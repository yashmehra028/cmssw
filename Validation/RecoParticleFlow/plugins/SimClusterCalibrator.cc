
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

class SimClusterCalibrator : public edm::global::EDProducer<> {
public:
  explicit SimClusterCalibrator(const edm::ParameterSet&);
  void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
  const edm::EDGetTokenT<SimClusterCollection> SimClusterToken_;
  const edm::EDGetTokenT<reco::PFRecHitCollection> RechitToken_;
	const edm::EDGetTokenT<edm::ValueMap<int>> RecHitMapToken_;

};

SimClusterCalibrator::SimClusterCalibrator(const edm::ParameterSet& iConfig) :
  SimClusterToken_(consumes<SimClusterCollection>(iConfig.getParameter<edm::InputTag>("simClusters"))),
  RechitToken_(consumes<reco::PFRecHitCollection>(iConfig.getParameter<edm::InputTag>("pfRecHits"))),
	RecHitMapToken_(consumes<edm::ValueMap<int>>(iConfig.getParameter<edm::InputTag>("pfRecHitMap")))
{
{
  produces<SimClusterCollection>("MergedCaloTruthCalibrated");
}

void SimClusterCalibrator::produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup& iSetup) const {
  
  auto CalibratedSimClusters = std::make_unique<SimClusterCollection>();

  edm::Handle<SimClusterCollection> SimClusters;
  iEvent.getByToken(SimClusterToken_, SimClusters);

  edm::Handle<reco::PFRecHitCollection> RecHits;
  iEvent.getByToken(RechitToken_, RecHits);

	edm::Handle<edm::ValueMap<int>> RecHitMap;
  iEvent.getByToken(RecHitMapToken_, RecHitMap);

  for (const auto& sc : *SimClusters) {
    // 1. Start with a copy to preserve momentum, particleId, eventId, etc.
    SimCluster CalibratedSC = sc;

    // 2. Clear the existing hit/energy/fraction vectors so we can replace them
    CalibratedSC.clearHitsAndEnergies();
    //CalibratedSC.clearFractions();

    // 3. Get the View from the ORIGINAL cluster (sc)
    // This provides the DetId and the SimFraction for each hit
    SimCluster::HitsAndFractionsView hafView = sc.hits_and_fractions_view();

    for (const auto& hit_fraction : hafView) {
      uint32_t detId = hit_fraction.first;
      double simFraction = hit_fraction.second;

      // 4. Find the corresponding PFRecHit (PFTester style)
      auto rechitIt = std::find_if(RecHits->begin(), RecHits->end(), 
                                   [detId](const reco::PFRecHit& rh) { 
                                     return rh.detId() == detId; 
                                   });

      double calibratedEnergy = 0.0;
      if (rechitIt != RecHits->end()) {
        // Calculate the calibrated energy: RecHit Energy * Sim Fraction
        calibratedEnergy = rechitIt->energy() * simFraction;
      }

      // 5. Add the data back into our new cluster
      // This fills hits_, energies_, and fractions_ in parallel
      CalibratedSC.addRecHitAndEnergy(detId, calibratedEnergy);
      //CalibratedSC.addHitFraction(simFraction);
    }

    CalibratedSC.finalizeHits();

    CalibratedSimClusters->push_back(CalibratedSC);
  }

  iEvent.put(std::move(CalibratedSimClusters), "MergedCaloTruthCalibrated");
}

DEFINE_FWK_MODULE(SimClusterCalibrator);
