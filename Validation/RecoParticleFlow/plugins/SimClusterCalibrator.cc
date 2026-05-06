
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "SimDataFormats/CaloAnalysis/interface/SimCluster.h"
#include "SimDataFormats/CaloAnalysis/interface/SimClusterFwd.h"
#include "DataFormats/ParticleFlowReco/interface/PFRecHit.h"
#include "DataFormats/ParticleFlowReco/interface/PFRecHitFwd.h"
#include "DataFormats/Common/interface/ValueMap.h"
#include <vector>
#include "FWCore/Framework/interface/MakerMacros.h"

class SimClusterCalibrator : public edm::global::EDProducer<> {
public:
  explicit SimClusterCalibrator(const edm::ParameterSet&);
  void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;
	static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);	

private:
  const edm::EDGetTokenT<SimClusterCollection> SimClusterToken_;
  //const edm::EDGetTokenT<reco::PFRecHitCollection> RechitToken_;
  std::vector<edm::EDGetTokenT<reco::PFRecHitCollection>> RechitTokens_;
	const edm::EDGetTokenT<edm::ValueMap<int>> RecHitMapToken_;

};

SimClusterCalibrator::SimClusterCalibrator(const edm::ParameterSet& iConfig) :
  SimClusterToken_(consumes<SimClusterCollection>(iConfig.getParameter<edm::InputTag>("simClusters"))),
  //RechitToken_(consumes<reco::PFRecHitCollection>(iConfig.getParameter<edm::InputTag>("pfRecHits"))),
  
	RecHitMapToken_(consumes<edm::ValueMap<int>>(iConfig.getParameter<edm::InputTag>("pfRecHitMap")))

{
  const auto& tags = iConfig.getParameter<std::vector<edm::InputTag>>("pfRecHits");
  for (const auto& tag : tags) {
    RechitTokens_.push_back(consumes<reco::PFRecHitCollection>(tag));
  }

  produces<SimClusterCollection>("MergedCaloTruthCalibrated");
}

void SimClusterCalibrator::produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup& iSetup) const {
  
  auto CalibratedSimClusters = std::make_unique<SimClusterCollection>();

  edm::Handle<SimClusterCollection> SimClusters;
  iEvent.getByToken(SimClusterToken_, SimClusters);

  std::vector<edm::Handle<reco::PFRecHitCollection>> RecHitHandles(RechitTokens_.size());
  for (size_t i = 0; i < RechitTokens_.size(); ++i) {
    iEvent.getByToken(RechitTokens_[i], RecHitHandles[i]);
  }

  for (const auto& sc : *SimClusters) {
    SimCluster CalibratedSC = sc;
    CalibratedSC.clearHitsAndFractions();
    CalibratedSC.clearHitsEnergy();

    auto hafView = sc.hits_and_fractions_view();

    for (const auto& hit_fraction : hafView) {
      uint32_t rawId = hit_fraction.first;
      float simFraction = hit_fraction.second;

      // Check all collections (ECAL, HCAL, etc.) for this specific DetId
      for (const auto& recHitHandle : RecHitHandles) {
        auto rechitIt = std::find_if(recHitHandle->begin(), recHitHandle->end(), 
                                     [rawId](const reco::PFRecHit& rh) { 
                                       return rh.detId() == rawId; 
                                     });

        if (rechitIt != recHitHandle->end()) {
          float calibratedEnergy = rechitIt->energy() * simFraction;
          
          // --- MOVED INSIDE: Only add the hit if it was found in the detector ---
          CalibratedSC.addRecHitAndFraction(rawId, simFraction);
          CalibratedSC.addHitEnergy(calibratedEnergy);
          
          break; // Found it, no need to check other collections for this hit
        }
      }
    }

    // Only save the cluster if it has at least one detectable hit remaining
    if (CalibratedSC.numberOfRecHits() == 0) continue;
    
    CalibratedSC.finalizeHits();
    CalibratedSimClusters->push_back(CalibratedSC);
    
  }

  iEvent.put(std::move(CalibratedSimClusters), "MergedCaloTruthCalibrated");
}

/*void SimClusterCalibrator::produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup& iSetup) const {
  
  auto CalibratedSimClusters = std::make_unique<SimClusterCollection>();

  std::cout << "\n=========================================================" << std::endl;
  std::cout << "--- EVENT: " << iEvent.id().event() << " | SimCluster Calibrator ---" << std::endl;
  std::cout << "=========================================================" << std::endl;

  edm::Handle<SimClusterCollection> SimClusters;
  iEvent.getByToken(SimClusterToken_, SimClusters);

  std::vector<edm::Handle<reco::PFRecHitCollection>> RecHitHandles(RechitTokens_.size());
  for (size_t i = 0; i < RechitTokens_.size(); ++i) {
    iEvent.getByToken(RechitTokens_[i], RecHitHandles[i]);
  }

  // Event-level stats
  double eventTotalLostE = 0.0;
  int eventTotalLostHits = 0;

  for (const auto& sc : *SimClusters) {
    SimCluster CalibratedSC = sc;
    
    // 1. Get views to compare Truth Energy vs Detector Energy
    auto originalEnergyView = sc.hits_and_energies_view();
    auto hafView = sc.hits_and_fractions_view();

    CalibratedSC.clearHitsAndFractions();
    CalibratedSC.clearHitsEnergy();

    std::cout << "\n[SimCluster PDG: " << sc.pdgId() << " | ID: " << sc.particleId() << "]" << std::endl;

    double clusterLostE = 0.0;
    int clusterLostHits = 0;

    // Iterate using index to keep energy and fraction views in sync
    for (size_t i = 0; i < hafView.size(); ++i) {
      uint32_t rawId = hafView[i].first;
      float simFraction = hafView[i].second;
      float simHitEnergy = originalEnergyView[i].second; // The Truth Energy

      // Identify Subdetector (HB, HE, EB, EE)
      DetId id(rawId);
      std::string label = "UNKN";
      if (id.det() == DetId::Hcal) {
          if (id.subdetId() == 1)      label = "HB  ";
          else if (id.subdetId() == 2) label = "HE  ";
          else label = "HCAL";
      } else if (id.det() == DetId::Ecal) {
          if (id.subdetId() == 1)      label = "EB  ";
          else if (id.subdetId() == 2) label = "EE  ";
          else label = "ECAL";
      }

      bool foundMatch = false;

      // Check all provided RecHit collections
      for (const auto& recHitHandle : RecHitHandles) {
        auto rechitIt = std::find_if(recHitHandle->begin(), recHitHandle->end(), 
                                     [rawId](const reco::PFRecHit& rh) { 
                                       return rh.detId() == rawId; 
                                     });

        if (rechitIt != recHitHandle->end()) {
          float calibratedEnergy = rechitIt->energy() * simFraction;
          
          CalibratedSC.addRecHitAndFraction(rawId, simFraction);
          CalibratedSC.addHitEnergy(calibratedEnergy);
          
          std::cout << "    [MATCH] " << label << " | DetId: " << rawId
                    << " | SimE: " << std::fixed << std::setprecision(4) << std::setw(7) << simHitEnergy
                    << " | RecHitE: " << std::setw(7) << rechitIt->energy()
                    << " | CALIB: " << std::setw(7) << calibratedEnergy << std::endl;

          foundMatch = true;
          break; 
        }
      }

      if (!foundMatch) {
        clusterLostE += simHitEnergy;
        clusterLostHits++;
        std::cout << "    [LOST!] " << label << " | DetId: " << rawId 
                  << " | SimE: " << std::fixed << std::setprecision(4) << std::setw(7) << simHitEnergy 
                  << " (No match in RecHits)" << std::endl;
      }
    }

    // Cluster Summary
    if (clusterLostHits > 0) {
      std::cout << "  >> Cluster Stats: " << clusterLostHits << " hits lost. Total Lost E: " << clusterLostE << " GeV" << std::endl;
      eventTotalLostE += clusterLostE;
      eventTotalLostHits += clusterLostHits;
    }

    if (CalibratedSC.numberOfRecHits() == 0) {
      std::cout << "  !!! Skipping Cluster: No hits survived calibration." << std::endl;
      continue;
    }
    
    CalibratedSC.finalizeHits();
    CalibratedSimClusters->push_back(CalibratedSC);
  }

  // Event Summary
  std::cout << "\n--- EVENT SUMMARY ---" << std::endl;
  std::cout << "Total Unmatched Hits: " << eventTotalLostHits << std::endl;
  if (eventTotalLostHits > 0) {
    std::cout << "Average Lost Hit Energy: " << (eventTotalLostE / eventTotalLostHits) << " GeV" << std::endl;
  }
  std::cout << "----------------------\n" << std::endl;

  iEvent.put(std::move(CalibratedSimClusters), "MergedCaloTruthCalibrated");
}*/

void SimClusterCalibrator::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  
  // Keep the single InputTag for SimClusters
  desc.add<edm::InputTag>("simClusters", edm::InputTag("mix", "MergedCaloTruth"));

  // Change to std::vector<edm::InputTag> to support VInputTag in Python
  // We provide both ECAL and HCAL as the default list
  desc.add<std::vector<edm::InputTag>>("pfRecHits", {
    edm::InputTag("hltParticleFlowRecHitECALUnseeded"),
    edm::InputTag("hltParticleFlowRecHitHBHE")
  });

  desc.add<edm::InputTag>("pfRecHitMap", edm::InputTag("hltRecHitMapProducer", "pfRecHitMap"));
  
  descriptions.add("SimClusterCalibrator", desc);
}

DEFINE_FWK_MODULE(SimClusterCalibrator);
