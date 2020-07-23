#include <KFSimple/SimpleFinder.h>
#include "PFTaskManager.h"

void PFTaskManager::Run(long long int nEvents) {
  std::cout << "PFTaskManager::Run" << std::endl;
  nEvents = nEvents < 0 ? in_tree_->GetEntries() : nEvents;

  for (long long iEvent = 0; iEvent < nEvents; ++iEvent) {
    in_tree_->GetEntry(iEvent);
      std::cout << "Event # " << iEvent << " out of " << nEvents <<  std::endl;

    auto* converter_in = ((ConverterIn*)tasks_.at(kInConverter));
    SetCuts(converter_in->GetCuts());

    SimpleFinder FCFinder;
    FCFinder.Init(converter_in->CreateInputContainer());
    FCFinder.SetCuts(converter_in->GetCuts());
    FCFinder.SortTracks();
    FCFinder.FindParticles();

    auto* converter_out = ((ConverterOut*)tasks_.at(kOutConverter));
    converter_out->SetCandidates(FCFinder.GetLambdas());
    converter_out->Exec();

//     out_tree_->Fill();
  } // Event loop
}