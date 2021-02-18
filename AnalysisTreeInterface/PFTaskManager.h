#ifndef KFPARTICLESIMPLE_ANALYSISTREEINTERFACE_PFTASKMANAGER_H_
#define KFPARTICLESIMPLE_ANALYSISTREEINTERFACE_PFTASKMANAGER_H_

#include "AnalysisTree/TaskManager.hpp"

#include "ConverterIn.h"
#include "ConverterOut.h"

class PFTaskManager : public AnalysisTree::TaskManager {

  enum eTasks {
    kInConverter = 0,
    kOutConverter
  };

 public:
  PFTaskManager() = default;
  PFTaskManager(PFTaskManager& other) = delete;
  void operator=(const PFTaskManager&) = delete;
  ~PFTaskManager() override = default;

  void Run(long long nEvents) final;

  void AddTasks(ConverterIn* in_task, ConverterOut* out_task) {
    assert(tasks_.empty());
    tasks_.emplace_back(in_task);
    tasks_.emplace_back(out_task);
  }

  void SetDecay(const DecayContainer& decay) { decay_ = decay; };
  void SetCuts(const CutsContainer& cuts) { cuts_ = cuts; };

  void AddTask(AnalysisTree::Task* task) = delete;//TODO make it virtual in AT

 protected:
  DecayContainer decay_;
  CutsContainer cuts_;
};

#endif//KFPARTICLESIMPLE_ANALYSISTREEINTERFACE_PFTASKMANAGER_H_
