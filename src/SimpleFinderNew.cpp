#include "SimpleFinderNew.hpp"
#include <KFParticleSIMD.h>

void SimpleFinderNew::Init(KFPTrackVector&& tracks, const KFVertex& pv) {
  tracks_ = tracks;
  prim_vx_ = pv;
  InitIndexesMap();
}

void SimpleFinderNew::SetTrack(const KFParticle& particle, int id, KFPTrackVector& tracks) {
  for (Int_t iP = 0; iP < 6; iP++) {
    tracks.SetParameter(particle.GetParameter(iP), iP, id);
  }
  for (Int_t iC = 0; iC < 21; iC++) {
    tracks.SetCovariance(particle.GetCovariance(iC), iC, id);
  }
  for (Int_t iF = 0; iF < 10; iF++) {
    tracks.SetFieldCoefficient(particle.GetFieldCoeff()[iF], iF, id);
  }
  tracks.SetPDG(particle.GetPDG(), id);
  tracks.SetQ(particle.GetQ(), id);
  tracks.SetPVIndex(-1, id);
  tracks.SetId(particle.Id(), id);
}

void SimpleFinderNew::Init(const InputContainer& input) {
  const std::vector<KFParticle>& tracks = input.GetTracks();
  KFPTrackVector track_tmp;
  track_tmp.Resize(tracks.size());
  for (size_t iTr = 0; iTr < tracks.size(); iTr++) {
    SetTrack(tracks[iTr], iTr, track_tmp);
  }
  Init(std::move(track_tmp), input.GetVertex());
}

float SimpleFinderNew::CalculateDistanceBetweenParticles(const Parameters_t& parameters) {
  const float dx = parameters.at(0).at(kX) - parameters.at(1).at(kX);
  const float dy = parameters.at(0).at(kY) - parameters.at(1).at(kY);
  const float dz = parameters.at(0).at(kZ) - parameters.at(1).at(kZ);
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void SimpleFinderNew::CalculateParamsInPCA(const KFPTrack& track1, int pid1, const KFPTrack& track2, int pid2) {
  params_ = Parameters_t(2);

  KFParticle particle1(track1, pid1);
  KFParticle particle2(track2, pid2);
  KFParticleSIMD particleSIMD1(particle1);// the same particle is copied to each SIMD element
  KFParticleSIMD particleSIMD2(particle2);

  float_v dS[2];
  float_v params1[8], params2[8];
  particleSIMD1.GetDStoParticleFast(particleSIMD2, dS);
  particleSIMD1.TransportFast(dS[0], params1);
  particleSIMD2.TransportFast(dS[1], params2);

  for (int i = 0; i < 8; i++) {
    params_.at(0).at(i) = params1[i][0];
    params_.at(1).at(i) = params2[i][0];
  }
}

KFParticleSIMD SimpleFinderNew::ConstructMother(const std::vector<KFPTrack>& tracks, const std::vector<Pdg_t>& pdgs) {
  const auto n = tracks.size();

  KFParticleSIMD mother;
  std::vector<KFParticle> particles{};
  std::vector<KFParticleSIMD> particles_simd{};

  for (size_t i = 0; i < n; ++i) {
    particles.emplace_back(KFParticle(tracks.at(i), pdgs.at(i)));
    particles.at(i).SetId(tracks.at(i).Id());
    particles_simd.emplace_back(particles.at(i));
  }
    
  if(n == 2) {
    float_v ds[2] = {0.f,0.f};
    float_v dsdr[4][6];
    particles_simd.at(0).GetDStoParticle( particles_simd.at(1), ds, dsdr );
    particles_simd.at(0).TransportToDS(ds[0], dsdr[0]);
    particles_simd.at(1).TransportToDS(ds[1], dsdr[3]);
  }
  else {
    auto sv = GetSecondaryVertex();
    float_v vertex[3] = {sv[0], sv[1], sv[2]};
    for (size_t i = 0; i < n; ++i) {
      float_v ds, dsdr[6];
      ds = particles_simd.at(i).GetDStoPoint(vertex, dsdr);
      particles_simd.at(i).TransportToDS(ds, dsdr);
    }
  }

  if (n == 2) {
    const KFParticleSIMD* vDaughtersPointer[2] = {&particles_simd.at(0), &particles_simd.at(1)};
    mother.Construct(vDaughtersPointer, 2, nullptr);
  } else if (n == 3) {
    const KFParticleSIMD* vDaughtersPointer[3] = {&particles_simd.at(0), &particles_simd.at(1), &particles_simd.at(2)};
    mother.Construct(vDaughtersPointer, 3, nullptr);
  }

  return mother;
}

float SimpleFinderNew::CalculateChiToPrimaryVertex(const KFPTrack& track, Pdg_t pid) const {
  // SIMD'ized version
  KFParticle tmpPart(track, pid);
  KFParticleSIMD tmpPartSIMD(tmpPart);

  KFVertex prim_vx_tmp = prim_vx_;
  const KFParticleSIMD prim_vx_Simd(prim_vx_tmp);
  const float_v point[3] = {prim_vx_Simd.X(), prim_vx_Simd.Y(), prim_vx_Simd.Z()};
  tmpPartSIMD.TransportToPoint(point);

  float_v chi2vec = tmpPartSIMD.GetDeviationFromVertex(prim_vx_Simd);
  return chi2vec[0];
}

std::vector<int> SimpleFinderNew::GetIndexes(const Daughter& daughter) {
  std::vector<int> result{};
  for (auto pid : daughter.GetPids()) {
    auto it = indexes_.find(pid);
    if (it != indexes_.end()) {
      for (auto i_track : it->second) {
        auto track = GetTrack(i_track);
        if (IsGoodDaughter(track, daughter)) {
          result.emplace_back(i_track);
        }
      }
    }
  }
  return result;
}

void SimpleFinderNew::InitIndexesMap() {
  for (int i = 0; i < tracks_.Size(); i++) {
    auto pdg = tracks_.PDG()[i];
    auto it = indexes_.find(pdg);
    if (it != indexes_.end()) {
      it->second.emplace_back(i);
    } else {
      indexes_[pdg] = {i};
    }
  }
}

bool SimpleFinderNew::IsGoodDaughter(const KFPTrack& track, const Daughter& daughter) {
  int id = daughter.GetId();
  values_.chi2_prim.at(id) = CalculateChiToPrimaryVertex(track, daughter.GetPdgHypo());
  if (values_.chi2_prim.at(id) < daughter.GetCutChi2Prim() || std::isnan(values_.chi2_prim.at(id))) { return false; }
  return true;
}

bool SimpleFinderNew::IsGoodPair(const KFPTrack& track1,
                                 const KFPTrack& track2,
                                 const Decay& decay) {
  const auto& daughters = decay.GetDaughters();
  CalculateParamsInPCA(track1, daughters[0].GetPdgHypo(), track2, daughters[1].GetPdgHypo());
  values_.distance[0] = CalculateDistanceBetweenParticles(params_);

  if (values_.distance[0] > decay.GetMother().GetCutDistance() || std::isnan(values_.distance[0])) { return false; }
  return true;
}

bool SimpleFinderNew::IsGoodMother(const KFParticleSIMD& kf_mother, const Mother& mother) {
  values_.chi2_geo = kf_mother.Chi2()[0] / simd_cast<float_v>(kf_mother.NDF())[0];
  if (values_.chi2_geo > mother.GetCutChi2Geo() || std::isnan(values_.chi2_geo)) { return false; }

  float_v l_Simd, dl_Simd;
  float_m isFromPV_Simd;
  KFVertex prim_vx_tmp = prim_vx_;
  const KFParticleSIMD prim_vx_Simd(prim_vx_tmp);

  kf_mother.GetDistanceToVertexLine(prim_vx_Simd, l_Simd, dl_Simd, &isFromPV_Simd);

  KFParticleSIMD motherTopo = kf_mother;
  motherTopo.SetProductionVertex(prim_vx_Simd);
  motherTopo.KFParticleBaseSIMD::GetDecayLength(l_Simd, dl_Simd);

  values_.l_over_dl = l_Simd[0] / dl_Simd[0];
  if (values_.l_over_dl < mother.GetCutLdL() || std::isnan(values_.l_over_dl)) { return false; }

  values_.chi2_topo = motherTopo.GetChi2()[0] / simd_cast<float_v>(motherTopo.GetNDF())[0];
  if (values_.chi2_topo > mother.GetCutChi2Topo() || std::isnan(values_.chi2_topo)) { return false; }

  values_.l = l_Simd[0];
  values_.cos_topo = CalculateCosTopo(kf_mother);
  values_.is_from_PV = isFromPV_Simd[0];

  return true;
}

void SimpleFinderNew::SaveParticle(KFParticleSIMD& particle_simd, const Decay& decay) {
  KFParticle particle;

  particle_simd.GetKFParticle(particle, 0);
  particle.SetPDG(decay.GetMother().GetPdg());

  OutputContainer mother(particle);
  mother.SetSelectionValues(values_);

  output_.emplace_back(mother);
  
  tracks_.Resize(tracks_.Size() + 1);
  SetTrack(particle, tracks_.Size() - 1, tracks_);
}

float SimpleFinderNew::CalculateCosTopo(const KFParticleSIMD& mother) const {
  const float px = mother.GetPx()[0];
  const float py = mother.GetPy()[0];
  const float pz = mother.GetPz()[0];

  const float delta_x = mother.GetX()[0] - prim_vx_.GetX();
  const float delta_y = mother.GetY()[0] - prim_vx_.GetY();
  const float delta_z = mother.GetZ()[0] - prim_vx_.GetZ();

  const float sp = delta_x * px + delta_y * py + delta_z * pz;
  const float norm = std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z) * mother.GetP()[0];
  return sp / norm;
}

bool SimpleFinderNew::IsGoodCos(const KFParticleSIMD& mother, const SimpleFinderNew::Parameters_t& daughter_pars, const Decay& decay) {
  for (int i = 0; i < decay.GetNDaughters(); ++i) {
    const auto cut = decay.GetDaughters()[i].GetCutCos();
    const auto& par = daughter_pars.at(i);
    const float norm = mother.GetP()[0] * std::sqrt(par[kPx] * par[kPx] + par[kPy] * par[kPy] + par[kPz] * par[kPz]);
    values_.cos[i] = (mother.GetPx()[0] * par[kPx] + mother.GetPy()[0] * par[kPy] + mother.GetPz()[0] * par[kPz]) / norm;
    if (values_.cos[i] < cut || std::isnan(values_.cos[i])) {
      return false;
    }
  }
  return true;
}

std::array<float, 3> SimpleFinderNew::GetSecondaryVertex() {
  if (params_.size() < 2 || params_.size() > 3) {
    throw std::runtime_error("Daughter parameters size is wrong");
  }
  std::array<float, 3> sv{};
  for (int i = 0; i < 3; ++i) {
    sv.at(i) = (params_[0].at(kX + i) + params_[1].at(kX + i)) / 2;
  }
  return sv;
}

void SimpleFinderNew::ReconstructDecay(const Decay& decay) {

  std::vector<std::vector<int>> indexes{};
  std::vector<Pdg_t> pdgs{};
  for (const auto& daughter : decay.GetDaughters()) {
    indexes.emplace_back(GetIndexes(daughter));
    pdgs.emplace_back(daughter.GetPdgHypo());
  }

  for (auto index_1 : indexes.at(0)) {
    auto track_1 = GetTrack(index_1);

    for (auto index_2 : indexes.at(1)) {
      auto track_2 = GetTrack(index_2);
      if (!IsGoodPair(track_1, track_2, decay)) continue;

      if (decay.GetNDaughters() == 2) {
        KFParticleSIMD kf_mother = ConstructMother({track_1, track_2}, pdgs);
        if (!IsGoodMother(kf_mother, decay.GetMother())) continue;
        if (!IsGoodCos(kf_mother, params_, decay)) continue;
        FillDaughtersInfo({track_1, track_2}, pdgs);
        SaveParticle(kf_mother, decay);
      } else if (decay.GetNDaughters() == 3) {
        for (auto index_3 : indexes.at(2)) {
          auto track_3 = GetTrack(index_3);
          if (!IsGoodThree(track_1, track_2, track_3, decay)) continue;

          KFParticleSIMD kf_mother = ConstructMother({track_1, track_2, track_3}, pdgs);
          if (!IsGoodMother(kf_mother, decay.GetMother())) continue;
          if (!IsGoodCos(kf_mother, params_, decay)) continue;
          FillDaughtersInfo({track_1, track_2, track_3}, pdgs);
          SaveParticle(kf_mother, decay);
        }
      } else {
        throw std::runtime_error("Number of daughters should be 2 or 3. Current number is " + std::to_string(decay.GetNDaughters()));
      }
    }
  }
}

void SimpleFinderNew::FillDaughtersInfo(const std::vector<KFPTrack>& tracks, const std::vector<Pdg_t>& pdgs) {
  for (int i = 0; i < tracks.size(); ++i) {
    values_.chi2_prim[i] = CalculateChiToPrimaryVertex(tracks.at(i), pdgs.at(i));
  }
}