#include "HCalCosmics.h"

#include <calobase/TowerInfo.h>
#include <calobase/TowerInfoContainer.h>

#include <fun4all/Fun4AllReturnCodes.h>
#include <fun4all/SubsysReco.h>

#include <phool/getClass.h>

#include <Math/SpecFuncMathCore.h>
#include <TF1.h>
#include <TFile.h>
#include <TH1.h>
#include <TH2.h>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace std;

HCalCosmics::HCalCosmics(const std::string &name, const std::string &fname)
  : SubsysReco(name)
  , outfilename(fname)
{
}

int HCalCosmics::Init(PHCompositeNode * /*topNode*/)
{
  std::cout << std::endl
            << "HCalCosmics::Init" << std::endl;
  std::cout << "Node with raw ADC: " << rawprefix + rawdetector << std::endl;
  std::cout << "Node with calibrated Energy in eV: " << prefix + detector << std::endl;
  std::cout << "Threshold: " << tower_threshold << " " << vert_threshold << " " << veto_threshold << std::endl;
  std::cout << "Bin width: " << bin_width << std::endl;

  outfile = new TFile(outfilename.c_str(), "RECREATE");

  for (int ieta = 0; ieta < n_etabin; ++ieta)
  {
    for (int iphi = 0; iphi < n_phibin; ++iphi)
    {
      std::string channel_histname = "h_channel_" + std::to_string(ieta) + "_" + std::to_string(iphi);
      h_channel_hist[ieta][iphi] = new TH1F(channel_histname.c_str(), "", 500, 0, 500 * bin_width); 

      std::string adc_histname = "h_adc_" + std::to_string(ieta) + "_" + std::to_string(iphi);
      h_adc_hist[ieta][iphi] = new TH1F(adc_histname.c_str(), "", 500, 0, 16000 * rawbin_width);
      
      std::string time_histname = "h_towertime_" + std::to_string(ieta) + "_" + std::to_string(iphi);
      h_towertime_hist[ieta][iphi] = new TH1F(time_histname.c_str(), "", 100, -10, 10);
   
    }
  }
  h_waveformchi2 = new TH2F("h_waveformchi2", "", 1000, 0, 500 * bin_width, 1000, 0, 1000000);
  h_waveformchi2->GetXaxis()->SetTitle("peak (ADC)");
  h_waveformchi2->GetYaxis()->SetTitle("chi2");
  h_waveformchi2_aftercut = new TH2F("h_waveformchi2_aftercut", "", 1000, 0, 500 * bin_width, 1000, 0, 1000000);
  h_waveformchi2_aftercut->GetXaxis()->SetTitle("peak (ADC)");
  h_waveformchi2_aftercut->GetYaxis()->SetTitle("chi2");
  h_mip = new TH1F("h_mip", "", 500, 0, 500 * bin_width);
  h_adc = new TH1F("h_adc", "", 500, 0, 16000 * rawbin_width);
  h_event = new TH1F("h_event", "", 1, 0, 1);

  h_time_energy = new TH2F("h_time_energy", "", 100, -10, 10, 100, -10 * bin_width, 90 * bin_width);

  event = 0;
  return 0;
}

int HCalCosmics::process_event(PHCompositeNode *topNode)
{
  process_towers(topNode);
  event++;
  h_event->Fill(0);

  return Fun4AllReturnCodes::EVENT_OK;
}

int HCalCosmics::process_towers(PHCompositeNode *topNode)
{

  //***************** Raw tower ADC (uncalibrated) info *****************

  std::string rawnode_name = rawprefix + rawdetector;

  TowerInfoContainer *rawtowers = findNode::getClass<TowerInfoContainer>(topNode, rawnode_name);
  if (!rawtowers)
  {
    std::cout << std::endl
              << "Didn't find node " << rawnode_name << std::endl;
    return Fun4AllReturnCodes::EVENT_OK;
  }

  //*****************  tower energy in eV (calibrated) info ***************

  std::string calibnode_name = prefix + detector;

  TowerInfoContainer *towers = findNode::getClass<TowerInfoContainer>(topNode, calibnode_name);
  if (!towers)
  {
    std::cout << std::endl
              << "Didn't find node " << calibnode_name << std::endl;
    return Fun4AllReturnCodes::EVENT_OK;
  }

  int size = towers->size(); // 1536 towers

  for (int channel = 0; channel < size; channel++)
  {
    TowerInfo *tower = towers->get_tower_at_channel(channel);
    TowerInfo *rawtower = rawtowers->get_tower_at_channel(channel);
    float adc = rawtower->get_energy();
    float energy = tower->get_energy();
    float chi2 = tower->get_chi2();
    float time = tower->get_time_float();
    unsigned int towerkey = towers->encode_key(channel);
    int ieta = towers->getTowerEtaBin(towerkey);
    int iphi = towers->getTowerPhiBin(towerkey);
    m_peak[ieta][iphi] = energy;
    m_adc[ieta][iphi] = adc;
    m_chi2[ieta][iphi] = chi2;
    m_time[ieta][iphi] = time;
    h_waveformchi2->Fill(m_peak[ieta][iphi], m_chi2[ieta][iphi]);
    if (tower->get_isBadChi2())
    {
      m_peak[ieta][iphi] = 0;
    }
    h_waveformchi2_aftercut->Fill(m_peak[ieta][iphi], m_chi2[ieta][iphi]);
    h_time_energy->Fill(time, energy);
  }
  // Apply cuts based on calibrated energy
  for (int ieta = 0; ieta < n_etabin; ++ieta)
  {
    for (int iphi = 0; iphi < n_phibin; ++iphi)
    {
      if (m_peak[ieta][iphi] < tower_threshold) 
      {
        continue;  // target tower cut
      }
      int up = iphi + 1;
      int down = iphi - 1;
      if (up > 63) { up -= 64; }
      if (down < 0) { down += 64; }
      if (m_peak[ieta][up] < vert_threshold || m_peak[ieta][down] < vert_threshold)
      {
        continue;  // vertical neighbor cut
      }
      if (ieta != 0 && (m_peak[ieta - 1][up] > veto_threshold || 
                        m_peak[ieta - 1][iphi] > veto_threshold || 
                        m_peak[ieta - 1][down] > veto_threshold))
      {
        continue;  // left veto cut
      }
      if (ieta != 23 && (m_peak[ieta + 1][up] > veto_threshold || 
                         m_peak[ieta + 1][iphi] > veto_threshold || 
                         m_peak[ieta + 1][down] > veto_threshold))
      {
        continue;  // right veto cut
      }
      h_channel_hist[ieta][iphi]->Fill(m_peak[ieta][iphi]);
      h_towertime_hist[ieta][iphi]->Fill(m_time[ieta][iphi]);
      h_mip->Fill(m_peak[ieta][iphi]);
    }
  }

  // Apply cuts based on raw tower ADC
  for (int ieta = 0; ieta < n_etabin; ++ieta)
  {
    for (int iphi = 0; iphi < n_phibin; ++iphi)
    {
      if (m_adc[ieta][iphi] < adc_tower_threshold)  
      {
        continue;   // target tower cut
      }
      int up = iphi + 1;
      int down = iphi - 1;
      if (up > 63) { up -= 64; }
      if (down < 0) { down += 64; }

      if (m_adc[ieta][up] < adc_vert_threshold || m_adc[ieta][down] < adc_vert_threshold)
        {
          continue;  // vertical neighbor cut
        }
      if (ieta != 0 && (m_adc[ieta - 1][up] > adc_veto_threshold ||
                        m_adc[ieta - 1][iphi] > adc_veto_threshold ||
                        m_adc[ieta - 1][down] > adc_veto_threshold))
        {
          continue;  // left veto cut
        }
      if (ieta != 23 && (m_adc[ieta + 1][up] > adc_veto_threshold ||
                         m_adc[ieta + 1][iphi] > adc_veto_threshold ||
                         m_adc[ieta + 1][down] > adc_veto_threshold))
        {
          continue;  // right veto cut
        }
        h_adc_hist[ieta][iphi]->Fill(m_adc[ieta][iphi]);
        h_adc->Fill(m_adc[ieta][iphi]);
    }
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

int HCalCosmics::End(PHCompositeNode * /*topNode*/)
{
  std::cout << "HCalCosmics::End" << std::endl;
  outfile->cd();
  for (auto &ieta : h_channel_hist)
  {
    for (auto &iphi : ieta)
    {
      iphi->Write();
      delete iphi;
    }
  }

  for (auto &ieta : h_towertime_hist)
  {
    for (auto &iphi : ieta)
    {
      iphi->Write();
      delete iphi;
    }
  }

  for (auto &ieta : h_adc_hist)
  {
    for (auto &iphi : ieta)
    {
      iphi->Write();
      delete iphi;
    }
  }
  	
  h_mip->Write();
  h_adc->Write();
  h_waveformchi2->Write();
  h_waveformchi2_aftercut->Write();
  h_time_energy->Write();
  h_event->Write();

  outfile->Close();
  delete outfile;
  return 0;
}

double HCalCosmics::gamma_function(const double *x, const double *par)
{
  double peak = par[0];
  double shift = par[1];
  double scale = par[2];
  double N = par[3];

  if (scale == 0)
  {
    return 0;
  }

  double arg_para = (x[0] - shift) / scale;
  arg_para = std::max<double>(arg_para, 0);
  double peak_para = (peak - shift) / scale;
  //  double numerator = N * pow(arg_para, peak_para) * TMath::Exp(-arg_para);
  double numerator = N * pow(arg_para, peak_para) * std::exp(-arg_para);
  double denominator = ROOT::Math::tgamma(peak_para + 1) * scale;

  if (denominator == 0)
  {
    return 1e8;
  }
  double val = numerator / denominator;
  if (std::isnan(val))
  {
    return 0;
  }
  return val;
}

TF1 *HCalCosmics::fitHist(TH1 *h)
{
  TF1 *f_gaus = new TF1("f_gaus", "gaus", 0, 10000);
  h->Fit(f_gaus, "QN", "", 0, 10000);

  TF1 *f_gamma = new TF1("f_gamma", gamma_function, 0, 5000, 4);
  f_gamma->SetParName(0, "Peak(ADC)");
  f_gamma->SetParName(1, "Shift");
  f_gamma->SetParName(2, "Scale");
  f_gamma->SetParName(3, "N");

  f_gamma->SetParLimits(0, 1000, 4000);
  f_gamma->SetParLimits(1, 500, 2000);
  f_gamma->SetParLimits(2, 200, 1000);
  f_gamma->SetParLimits(3, 0, 1e9);

  f_gamma->SetParameter(0, f_gaus->GetParameter(1));
  f_gamma->SetParameter(1, 1300);
  f_gamma->SetParameter(2, 1300);
  f_gamma->SetParameter(3, 1e6);

  h->Fit(f_gamma, "RQN", "", 1000, 5000);

  return f_gamma;
}

void HCalCosmics::fitChannels(const std::string &infile, const std::string &outfilename2)
{
  TFile *fin = new TFile(infile.c_str(), "READ");
  if (!fin)
  {
    std::cout << "file " << infile << "   not found";
    return;
  }
  for (int ieta = 0; ieta < n_etabin; ++ieta)
  {
    for (int iphi = 0; iphi < n_phibin; ++iphi)
    {
      std::string channel_histname = "h_channel_" + std::to_string(ieta) + "_" + std::to_string(iphi);
      fin->GetObject(channel_histname.c_str(), h_channel_hist[ieta][iphi]);
    }
  }

  if (!h_channel_hist[0][0])
  {
    std::cout << "no hists in " << infile << std::endl;
    return;
  }

  TFile *outfileFit = new TFile(outfilename2.c_str(), "recreate");

  TH2 *h2_peak = new TH2F("h2_peak", "", n_etabin, 0, n_etabin, n_phibin, 0, n_phibin);

  TH1 *h_allTow = (TH1 *) h_channel_hist[0][0]->Clone("h_allTow");
  h_allTow->Reset();

  for (int ieta = 0; ieta < n_etabin; ++ieta)
  {
    for (int iphi = 0; iphi < n_phibin; ++iphi)
    {
      TF1 *res = fitHist(h_channel_hist[ieta][iphi]);
      h2_peak->SetBinContent(ieta + 1, iphi + 1, res->GetParameter(0));
      h_allTow->Add(h_channel_hist[ieta][iphi]);
    }
  }

  TF1 *res = fitHist(h_allTow);
  res->Write("f_allTow");

  h_allTow->Write();
  h2_peak->Write();

  outfileFit->Close();
  fin->Close();
}


