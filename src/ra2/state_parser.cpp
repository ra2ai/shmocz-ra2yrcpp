#include "ra2/state_parser.hpp"

#include "ra2yrproto/ra2yr.pb.h"

#include "constants.hpp"
#include "logging.hpp"
#include "protocol/helpers.hpp"
#include "ra2/abi.hpp"
#include "ra2/event_list.hpp"
#include "ra2/yrpp_export.hpp"
#include "utility/array_iterator.hpp"
#include "utility/serialize.hpp"

#include <fmt/core.h>

#undef GetMessage

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

using namespace ra2;

ClassParser::ClassParser(Cookie c, ra2yrproto::ra2yr::Object* T) : c(c), T(T) {}

void Cell::copy_to(ra2yrproto::ra2yr::Cell* dst, const Cell* src) {
  dst->set_land_type(static_cast<ra2yrproto::ra2yr::LandType>(src->land_type));
  dst->set_radiation_level(src->radiation_level);
  dst->set_height(static_cast<i32>(src->height));
  dst->set_level(static_cast<i32>(src->level));
  dst->set_overlay_data(src->overlay_data);
  dst->set_tiberium_value(src->tiberium_value);
  dst->set_shrouded(src->shrouded);
  dst->set_passability(src->passability);
  dst->set_index(src->index);
  if (src->first_object != 0U) {
    dst->mutable_objects()->Clear();
    dst->add_objects()->set_pointer_self(src->first_object);
  }
  dst->set_wall_owner_index(src->wall_owner_index);
  dst->set_overlay_type_index(src->overlay_type_index);
  dst->set_visibility(static_cast<i32>(src->visibility));
}

void ClassParser::Abstract() {
  auto* P = reinterpret_cast<AbstractClass*>(c.src);
  (void)P;
}

void ClassParser::Object() {
  Abstract();
  auto* P = reinterpret_cast<ObjectClass*>(c.src);
  T->set_health(P->Health);
  T->set_selected(P->IsSelected);
  T->set_in_limbo(P->InLimbo);
  T->set_on_map(P->IsOnMap);
  if (P->IsOnMap ||
      T->object_type() == ra2yrproto::ra2yr::ABSTRACT_TYPE_OVERLAY) {
    auto* q = T->mutable_coordinates();
    auto L = P->Location;
    q->set_x(L.X);
    q->set_y(L.Y);
    q->set_z(L.Z);
  }
}

void ClassParser::Mission() {
  Object();

  auto* P = reinterpret_cast<MissionClass*>(c.src);
  T->set_current_mission(
      static_cast<ra2yrproto::ra2yr::Mission>(P->CurrentMission));
}

void ClassParser::Radio() { Mission(); }

void ClassParser::Techno() {
  Radio();
  auto* P = reinterpret_cast<TechnoClass*>(c.src);
  T->set_pointer_house(reinterpret_cast<u32>(P->Owner));
  T->set_pointer_initial_owner(reinterpret_cast<u32>(P->InitialOwner));
  T->set_armor_multiplier(P->ArmorMultiplier);
  T->set_firepower_multiplier(P->FirepowerMultiplier);
  T->set_veterancy(P->Veterancy.Veterancy);
  T->set_veterancy_rank(static_cast<ra2yrproto::ra2yr::VeterancyRank>(
      P->Veterancy.GetRemainingLevel()));
  T->set_cloak_state(static_cast<ra2yrproto::ra2yr::CloakState>(P->CloakState));
}

void ClassParser::Foot() {
  Techno();
  auto* P = reinterpret_cast<FootClass*>(c.src);
  if (P->Destination != nullptr) {
    auto t = ra2::abi::AbstractClass_WhatAmI::call(c.abi, P->Destination);

    if (t == CellClass::AbsID) {
      auto* dest = reinterpret_cast<CellClass*>(P->Destination);
      auto coord = dest->Cell2Coord(dest->MapCoords);
      auto dd = T->mutable_destination();
      dd->set_x(coord.X);
      dd->set_y(coord.Y);
      dd->set_z(coord.Z);
    }
  }
  T->set_speed_percentage(P->SpeedPercentage);
  T->set_speed_multiplier(P->SpeedMultiplier);
}

void ClassParser::Aircraft() {
  Foot();
  auto* P = reinterpret_cast<AircraftClass*>(c.src);
  set_type_class(P->Type, ra2yrproto::ra2yr::ABSTRACT_TYPE_AIRCRAFT);
}

void ClassParser::Unit() {
  Foot();
  auto* P = reinterpret_cast<UnitClass*>(c.src);
  set_type_class(P->Type, ra2yrproto::ra2yr::ABSTRACT_TYPE_UNIT);
  T->set_deployed(P->Deployed);
  T->set_deploying(P->IsDeploying);
}

void ClassParser::Building() {
  Techno();
  auto* P = reinterpret_cast<BuildingClass*>(c.src);
  set_type_class(P->Type, ra2yrproto::ra2yr::ABSTRACT_TYPE_BUILDING);
}

void ClassParser::Infantry() {
  Foot();
  auto* P = reinterpret_cast<InfantryClass*>(c.src);
  set_type_class(P->Type, ra2yrproto::ra2yr::ABSTRACT_TYPE_INFANTRY);
}

void ClassParser::SuperWeapon() {
  Abstract();
  auto* P = reinterpret_cast<SuperClass*>(c.src);
  set_type_class(P->Type, ra2yrproto::ra2yr::ABSTRACT_TYPE_SUPER);
  T->set_pointer_house(reinterpret_cast<u32>(P->Owner));
  T->set_granted(P->Granted);
  T->set_one_time(P->OneTime);
  T->set_is_charged(P->IsCharged);
  T->set_is_on_hold(P->IsOnHold);
  T->set_time_left(P->RechargeTimer.GetTimeLeft());
}

void ClassParser::Terrain() {
  Object();
  auto* P = reinterpret_cast<TerrainClass*>(c.src);
  set_type_class(P->Type, ra2yrproto::ra2yr::ABSTRACT_TYPE_TERRAIN);
}

void ClassParser::parse() {
  T->set_pointer_self(reinterpret_cast<u32>(c.src));
  auto t = ra2::abi::AbstractClass_WhatAmI::call(
      c.abi, reinterpret_cast<AbstractClass*>(c.src));

  if (t == UnitClass::AbsID) {
    Unit();
  } else if (t == BuildingClass::AbsID) {
    Building();
  } else if (t == InfantryClass::AbsID) {
    Infantry();
  } else if (t == AircraftClass::AbsID) {
    Aircraft();
  } else if (t == SuperClass::AbsID) {
    SuperWeapon();
  } else if (t == TerrainClass::AbsID) {
    Terrain();
  } else {
    // eprintf("unknown ObjectClass: {}", static_cast<int>(t));
  }
}

void ClassParser::set_type_class(void* ttc, ra2yrproto::ra2yr::AbstractType t) {
  T->set_object_type(t);
  T->set_pointer_technotypeclass(reinterpret_cast<u32>(ttc));
}

TypeClassParser::TypeClassParser(Cookie c,
                                 ra2yrproto::ra2yr::ObjectTypeClass* T)
    : c(c), T(T) {}

void TypeClassParser::AbstractType() {
  auto* P = reinterpret_cast<AbstractTypeClass*>(c.src);
  T->set_name(P->Name);
}

void TypeClassParser::AircraftType() {
  TechnoType();
  auto* P = reinterpret_cast<AircraftTypeClass*>(c.src);

  T->set_array_index(P->ArrayIndex);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_AIRCRAFTTYPE);
}

static void parse_AbilitiesStruct(ra2yrproto::ra2yr::AbilitiesStruct* dst,
                                  const AbilitiesStruct& src) {
  dst->set_faster(src.FASTER);
  dst->set_stronger(src.STRONGER);
  dst->set_firepower(src.FIREPOWER);
  dst->set_scatter(src.SCATTER);
  dst->set_rof(src.ROF);
  dst->set_sight(src.SIGHT);
  dst->set_cloak(src.CLOAK);
  dst->set_tiberium_proof(src.TIBERIUM_PROOF);
  dst->set_vein_proof(src.VEIN_PROOF);
  dst->set_self_heal(src.SELF_HEAL);
  dst->set_explodes(src.EXPLODES);
  dst->set_radar_invisible(src.RADAR_INVISIBLE);
  dst->set_sensors(src.SENSORS);
  dst->set_fearless(src.FEARLESS);
  dst->set_c4(src.C4);
  dst->set_tiberium_heal(src.TIBERIUM_HEAL);
  dst->set_guard_area(src.GUARD_AREA);
  dst->set_crusher(src.CRUSHER);
}

void TypeClassParser::TechnoType() {
  ObjectType();
  auto* P = reinterpret_cast<TechnoTypeClass*>(c.src);
  T->set_cost(P->Cost);
  T->set_soylent(P->Soylent);
  auto A = abi::DVCIterator(&P->Prerequisite);
  T->mutable_prerequisites()->Clear();
  for (auto r : A) {
    T->add_prerequisites(static_cast<int>(r));
  }
  T->set_required_houses(P->RequiredHouses);
  T->set_forbidden_houses(P->ForbiddenHouses);
  T->set_owner_flags(P->OwnerFlags);
  T->set_tech_level(P->TechLevel);
  T->set_build_limit(P->BuildLimit);
  T->set_naval(P->Naval);
  T->set_requires_stolen_allied_tech(P->RequiresStolenAlliedTech);
  T->set_requires_stolen_soviet_tech(P->RequiresStolenSovietTech);
  T->set_requires_stolen_third_tech(P->RequiresStolenThirdTech);
  T->set_strength(P->Strength);
  T->set_sight(P->Sight);
  T->set_guard_range(P->GuardRange);
  T->set_build_time_multiplier(P->BuildTimeMultiplier);
  T->set_techno_rot(P->ROT);
  T->set_storage(P->Storage);

  T->set_teleporter(P->Teleporter);
  T->set_bunkerable(P->Bunkerable);
  T->set_immune_to_psionics(P->ImmuneToPsionics);

  T->set_speed(P->Speed);
  T->set_speed_type(static_cast<ra2yrproto::ra2yr::SpeedType>(P->SpeedType));
  T->set_movement_zone([](MovementZone mz) {
    // no negative enum value
    if (mz == MovementZone::None) {
      return ra2yrproto::ra2yr::MovementZone::MOVEMENT_ZONE_None;
    }
    return static_cast<ra2yrproto::ra2yr::MovementZone>(mz);
  }(P->MovementZone));

  T->set_prevent_attack_move(P->PreventAttackMove);
  T->set_opportunity_fire(P->OpportunityFire);

  if (P->Weapon[0].WeaponType) {
    T->set_pointer_primary_weapon(
        reinterpret_cast<u32>(P->Weapon[0].WeaponType));
  }
  if (P->EliteWeapon[0].WeaponType) {
    T->set_pointer_elite_primary_weapon(
        reinterpret_cast<u32>(P->EliteWeapon[0].WeaponType));
  }

  if (P->Weapon[1].WeaponType) {
    T->set_pointer_secondary_weapon(
        reinterpret_cast<u32>(P->Weapon[1].WeaponType));
  }
  if (P->EliteWeapon[1].WeaponType) {
    T->set_pointer_elite_secondary_weapon(
        reinterpret_cast<u32>(P->EliteWeapon[1].WeaponType));
  }

  T->set_naval_targeting(
      static_cast<ra2yrproto::ra2yr::LandTargetingType>(P->NavalTargeting));
  T->set_land_targeting(
      static_cast<ra2yrproto::ra2yr::NavalTargetingType>(P->LandTargeting));
  if (P->AirstrikeTeamType) {
    T->set_pointer_airstrike_team_type(
        reinterpret_cast<u32>(P->AirstrikeTeamType));
  }
  T->set_airstrike_team(P->AirstrikeTeam);
  T->set_airstrike_recharge_time(P->AirstrikeRechargeTime);

  if (P->EliteAirstrikeTeamType) {
    T->set_pointer_elite_airstrike_team_type(
        reinterpret_cast<u32>(P->EliteAirstrikeTeamType));
  }
  T->set_elite_airstrike_team(P->EliteAirstrikeTeam);
  T->set_elite_airstrike_recharge_time(P->EliteAirstrikeRechargeTime);

  T->set_ifv_mode(P->IFVMode);
  T->set_gunner(P->Gunner);

  T->set_omni_crusher(P->OmniCrusher);
  T->set_omni_crush_resistant(P->OmniCrushResistant);

  T->set_gap_generator(P->GapGenerator);
  T->set_gap_radius_in_cells(P->GapRadiusInCells);
  T->set_super_gap_radius_in_cells(P->SuperGapRadiusInCells);

  T->set_can_disguise(P->CanDisguise);
  T->set_perma_disguise(P->PermaDisguise);
  T->set_disguise_when_still(P->DisguiseWhenStill);
  T->set_detect_disguise(P->DetectDisguise);
  T->set_detect_disguise_range(P->DetectDisguiseRange);

  T->set_underwater(P->Underwater);
  T->set_cloakable(P->Cloakable);
  T->set_cloaking_speed(P->CloakingSpeed);
  T->set_sensors_sight(P->SensorsSight);

  T->set_pointer_spawns(reinterpret_cast<u32>(P->Spawns));
  T->set_spawns_number(P->SpawnsNumber);
  T->set_spawn_regen_rate(P->SpawnRegenRate);
  T->set_spawn_reload_rate(P->SpawnReloadRate);

  T->set_pointer_enslaves(reinterpret_cast<u32>(P->Enslaves));
  T->set_slaves_number(P->SlavesNumber);
  T->set_slave_regen_rate(P->SlaveRegenRate);
  T->set_slave_reload_rate(P->SlaveReloadRate);

  T->set_pointer_deploys_into(reinterpret_cast<u32>(P->DeploysInto));
  T->set_pointer_undeploys_into(reinterpret_cast<u32>(P->UndeploysInto));
  T->set_deploy_to_land(P->DeployToLand);

  T->set_passengers(P->Passengers);
  T->set_size_limit(P->SizeLimit);
  T->set_deploy_time(P->DeployTime);
  T->set_open_topped(P->OpenTopped);
  T->set_open_transport_weapon(P->OpenTransportWeapon);

  T->set_flight_level(P->FlightLevel);

  parse_AbilitiesStruct(T->mutable_veteran_abilities(), P->VeteranAbilities);
  parse_AbilitiesStruct(T->mutable_elite_abilities(), P->EliteAbilities);
}

void TypeClassParser::UnitType() {
  TechnoType();
  auto* P = reinterpret_cast<UnitTypeClass*>(c.src);

  T->set_array_index(P->ArrayIndex);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_UNITTYPE);
  T->set_harvester(P->Harvester);
  T->set_is_simple_deployer(P->IsSimpleDeployer);
}

void TypeClassParser::InfantryType() {
  TechnoType();
  auto* P = reinterpret_cast<InfantryTypeClass*>(c.src);

  T->set_array_index(P->ArrayIndex);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_INFANTRYTYPE);
  T->set_fearless(P->Fearless);
  T->set_harvest_rate(P->HarvestRate);

  T->set_engineer(P->Engineer);
  T->set_c4(P->C4);
  T->set_agent(P->Agent);
  T->set_civilian(P->Civilian);

  T->set_occupier(P->Occupier);
  T->set_pointer_occupy_weapon(
      reinterpret_cast<u32>(P->OccupyWeapon.WeaponType));
  T->set_pointer_elite_occupy_weapon(
      reinterpret_cast<u32>(P->EliteOccupyWeapon.WeaponType));

  T->set_deployer(P->Deployer);
  T->set_deployed_crushable(P->DeployedCrushable);
}

void TypeClassParser::ObjectType() {
  AbstractType();
  auto* P = reinterpret_cast<ObjectTypeClass*>(c.src);
  T->set_armor_type(static_cast<ra2yrproto::ra2yr::Armor>(P->Armor));
  T->set_strength(P->Strength);  // duplicate? OverlayTypeClass::Strength
  T->set_crushable(P->Crushable);
}

void TypeClassParser::BuildingType() {
  TechnoType();
  auto* P = reinterpret_cast<BuildingTypeClass*>(c.src);
  T->set_array_index(P->ArrayIndex);
  T->set_power_drain(P->PowerDrain);
  T->set_power_bonus(P->PowerBonus);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_BUILDINGTYPE);
  T->set_is_base_defense(P->IsBaseDefense);
  T->set_wall(P->Wall);
  T->set_build_category(
      static_cast<ra2yrproto::ra2yr::BuildCategory>(P->BuildCat));
  T->set_base_normal(P->BaseNormal);
  T->set_eligible_for_ally_building(P->EligibileForAllyBuilding);
  T->set_number_of_docks(P->NumberOfDocks);
  T->set_infantry_absorb(P->InfantryAbsorb);
  T->set_super_weapon(P->SuperWeapon);
  T->set_max_number_occupants(P->MaxNumberOccupants);

  T->set_gate(P->Gate);
  T->set_construction_yard(P->ConstructionYard);
  T->set_refinery(P->Refinery);
  T->set_radar(P->Radar);
  T->set_spy_sat(P->SpySat);
  T->set_bunker(P->Bunker);

  T->set_adjacent(P->Adjacent);
  T->set_foundation(static_cast<ra2yrproto::ra2yr::Foundation>(P->Foundation));

  T->set_produce_cash_startup(P->ProduceCashStartup);
  T->set_produce_cash_amount(P->ProduceCashAmount);
  T->set_produce_cash_delay(P->ProduceCashDelay);

  T->set_unit_repair(P->UnitRepair);
  T->set_units_gain_self_heal(P->UnitsGainSelfHeal);
  T->set_infantry_gain_self_heal(P->InfantryGainSelfHeal);
}

void TypeClassParser::OverlayType() {
  ObjectType();
  auto* P = reinterpret_cast<OverlayTypeClass*>(c.src);
  T->set_array_index(P->ArrayIndex);
  T->set_wall(P->Wall);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_OVERLAYTYPE);
  T->set_crate_trigger(P->CrateTrigger);
}

void TypeClassParser::SuperWeaponType() {
  AbstractType();
  auto* P = reinterpret_cast<SuperWeaponTypeClass*>(c.src);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_SUPERWEAPONTYPE);
  T->set_array_index(P->ArrayIndex);
  T->set_super_weapon_type(
      static_cast<ra2yrproto::ra2yr::SuperWeaponType>(P->Type));
  T->set_is_powered(P->IsPowered);
  T->set_recharge_time(P->RechargeTime);
  T->set_super_weapon_range(P->Range);
}

void TypeClassParser::WeaponType() {
  AbstractType();
  auto* P = reinterpret_cast<WeaponTypeClass*>(c.src);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_WEAPONTYPE);
  T->set_burst(P->Burst);
  if (P->Projectile) {
    T->set_pointer_bullet(reinterpret_cast<u32>(P->Projectile));
  }
  T->set_damage(P->Damage);
  T->set_speed(P->Speed);
  if (P->Warhead) {
    T->set_pointer_warhead(reinterpret_cast<u32>(P->Warhead));
  }
  T->set_rof(P->ROF);
  T->set_weapon_range(P->Range);
  T->set_minimum_range(P->MinimumRange);
  T->set_omni_fire(P->OmniFire);
  T->set_is_sonic(P->IsSonic);
  T->set_spawner(P->Spawner);
  T->set_cell_range_finding(P->CellRangefinding);
  T->set_infinite_mind_control(P->InfiniteMindControl);
  T->set_fire_while_moving(P->FireWhileMoving);
  T->set_suicide(P->Suicide);
  T->set_is_laser(P->IsLaser);
  T->set_area_fire(P->AreaFire);
}

// https://github.com/Phobos-developers/YRpp/blob/b5f8a49939d97bdadbf055c5cb2758cbbfccc4a6/WarheadTypeClass.h#L14-L20
struct VersesData {
  double Verses;
  // WarheadFlags
  bool : 8;
  bool : 8;
  bool : 8;
};

void TypeClassParser::WarheadType() {
  AbstractType();
  auto* P = reinterpret_cast<WarheadTypeClass*>(c.src);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_WARHEADTYPE);
  T->clear_verses();
  for (int i = 0; i < 11; i++) {
    // https://github.com/Phobos-developers/Phobos/blob/a87e52a2dca26f9a1cabe0b0002cddb385654ed5/src/Utilities/GeneralUtils.cpp#L74-L92
    // YRpp: PROTECTED_PROPERTY(DWORD, unused_1CC);
    auto ext_array = *reinterpret_cast<std::vector<VersesData>*>(
        *reinterpret_cast<uintptr_t*>(reinterpret_cast<char*>(P) + 0x1CC) +
        0x24);
    T->add_verses(ext_array.at(i).Verses);
  }
  T->set_cell_spread(P->CellSpread);
  T->set_percent_at_max(P->PercentAtMax);
  T->set_wall(P->Wall);
  T->set_wood(P->Wood);
  T->set_em_effect(P->EMEffect);
  T->set_mind_control(P->MindControl);
  T->set_poison(P->Poison);
  T->set_ivan_bomb(P->IvanBomb);
  T->set_parasite(P->Parasite);
  T->set_temporal(P->Temporal);
  T->set_airstrike(P->Airstrike);
  T->set_psychedelic(P->Psychedelic);
  T->set_radiation(P->Radiation);
  T->set_affects_allies(P->AffectsAllies);
}

void TypeClassParser::TerrainType() {
  ObjectType();
  auto* P = reinterpret_cast<TerrainTypeClass*>(c.src);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_TERRAINTYPE);
  T->set_array_index(P->ArrayIndex);
  T->set_spawns_tiberium(P->SpawnsTiberium);
  T->set_foundation(static_cast<ra2yrproto::ra2yr::Foundation>(
      P->Foundation));  // YRpp: use Foundation instead of int
}

void TypeClassParser::BulletType() {
  ObjectType();
  auto* P = reinterpret_cast<BulletTypeClass*>(c.src);
  T->set_type(ra2yrproto::ra2yr::ABSTRACT_TYPE_BULLETTYPE);
  T->set_arcing(P->Arcing);
  T->set_inviso(P->Inviso);
  T->set_inaccurate(P->Inaccurate);
  T->set_aa(P->AA);
  T->set_ag(P->AG);
  T->set_projectile_rot(P->ROT);
  T->set_subject_to_cliffs(P->SubjectToCliffs);
  T->set_subject_to_elevation(P->SubjectToElevation);
  T->set_subject_to_walls(P->SubjectToWalls);
}

void TypeClassParser::parse() {
  T->set_pointer_self(reinterpret_cast<u32>(c.src));
  auto t = ra2::abi::AbstractClass_WhatAmI::call(
      c.abi, reinterpret_cast<AbstractClass*>(c.src));

  if (t == BuildingTypeClass::AbsID) {
    BuildingType();
  } else if (t == AircraftTypeClass::AbsID) {
    AircraftType();
  } else if (t == InfantryTypeClass::AbsID) {
    InfantryType();
  } else if (t == UnitTypeClass::AbsID) {
    UnitType();
  } else if (t == OverlayTypeClass::AbsID) {
    OverlayType();
  } else if (t == SuperWeaponTypeClass::AbsID) {
    SuperWeaponType();
  } else if (t == WeaponTypeClass::AbsID) {
    WeaponType();
  } else if (t == WarheadTypeClass::AbsID) {
    WarheadType();
  } else if (t == TerrainTypeClass::AbsID) {
    TerrainType();
  } else if (t == BulletTypeClass::AbsID) {
    BulletType();
  } else {
    eprintf("unknown TypeClass: {}", static_cast<int>(t));
  }
}

EventParser::EventParser(const EventClass* src, ra2yrproto::ra2yr::Event* T,
                         u32 time)
    : src(src), T(T), time(time) {}

static void parse_TargetClass(const TargetClass& s,
                              ra2yrproto::ra2yr::TargetClass* d) {
  d->set_m_id(s.m_ID);
  d->set_m_rtti(s.m_RTTI);
}

template <typename EventT, typename MessageT>
static auto parse_MegaMission_common(const EventT& e, MessageT* m) {
  parse_TargetClass(e.Whom, m->mutable_whom());
  m->set_mission(static_cast<ra2yrproto::ra2yr::Mission>(e.Mission));
  parse_TargetClass(e.Target, m->mutable_target());
  parse_TargetClass(e.Destination, m->mutable_destination());
  return std::make_tuple(e, m);
}

void EventParser::MegaMission() {
  if (src->Type == EventType::MEGAMISSION) {
    auto [x, m] = parse_MegaMission_common(src->Data.MegaMission,
                                           T->mutable_mega_mission());
    parse_TargetClass(x.Follow, m->mutable_follow());
    m->set_is_planning_event(x.IsPlanningEvent);
  } else if (src->Type == EventType::MEGAMISSION_F) {
    auto [x, m] = parse_MegaMission_common(src->Data.MegaMission_F,
                                           T->mutable_mega_mission_f());
    m->set_speed(x.Speed);
    m->set_max_speed(x.MaxSpeed);
  } else {
    throw std::runtime_error(fmt::format(
        "invalid event type: {}", static_cast<unsigned char>(src->Type)));
  }
}

void EventParser::Deploy() {
  auto* d = T->mutable_deploy();
  parse_TargetClass(src->Data.Deploy.Whom, d->mutable_whom());
}

void EventParser::Sell() {
  const auto& x = src->Data.Sell;
  auto* d = T->mutable_sell();
  parse_TargetClass(x.Whom, d->mutable_whom());
}

void EventParser::Production() {
  const auto& x = src->Data.Production;
  auto* d = T->mutable_production();
  d->set_rtti_id(x.RTTI_ID);
  d->set_heap_id(x.Heap_ID);
  d->set_is_naval(x.IsNaval);
}

void EventParser::Place() {
  const auto& x = src->Data.Place;
  auto* d = T->mutable_place();
  d->set_rtti_type(static_cast<ra2yrproto::ra2yr::AbstractType>(x.RTTIType));
  d->set_heap_id(x.HeapID);
  d->set_is_naval(x.IsNaval);
  auto& ll = x.Location;
  auto* loc = d->mutable_location();
  loc->set_x(ll.X);
  loc->set_y(ll.Y);
}

void EventParser::parse() {
  T->set_event_type(static_cast<ra2yrproto::ra2yr::NetworkEvent>(src->Type));
  T->set_is_executed(src->IsExecuted);
  T->set_house_index(src->HouseIndex);
  T->set_frame(src->Frame);
  T->set_timing(time);
  switch (src->Type) {
    case EventType::DEPLOY:
      Deploy();
      break;
    case EventType::PRODUCE:
      Production();
      break;
    case EventType::PLACE:
      Place();
      break;
    case EventType::SELL:
      Sell();
      break;
    case EventType::MEGAMISSION:
      MegaMission();
      break;
    case EventType::MEGAMISSION_F:
      MegaMission();
      break;
    default:
      break;
  }
}

static void parse_Cell(Cell* C, int ix, const CellClass& cc) {
  C->radiation_level = cc.RadLevel;
  C->land_type = static_cast<i32>(cc.LandType);
  C->height = cc.Height;
  C->level = cc.Level;
  C->overlay_data = cc.OverlayData;
  C->tiberium_value = 0;
  int ff = static_cast<int>(cc.AltFlags);
  C->shrouded = ((ff & 0x8) == 0);
  C->passability = cc.Passability;
  C->index = ix;
  if (C->land_type ==
      static_cast<int>(ra2yrproto::ra2yr::LandType::LAND_TYPE_Tiberium)) {
    C->tiberium_value = ra2::abi::get_tiberium_value(cc);
  }
  C->first_object = reinterpret_cast<std::uintptr_t>(cc.FirstObject);
  C->wall_owner_index = cc.WallOwnerIndex;
  C->overlay_type_index = cc.OverlayTypeIndex;
  C->visibility = cc.Visibility;
}

std::vector<CellClass*> ra2::get_valid_cells(MapClass* M) {
  std::vector<CellClass*> res;
  auto L = M->MapCoordBounds;
  // Initialize valid cells
  for (int j = 0; j <= L.Bottom; j++) {
    for (int i = 0; i <= L.Right; i++) {
      CellStruct coords{static_cast<i16>(i), static_cast<i16>(j)};
      auto* cc = M->TryGetCellAt(coords);
      if (cc != nullptr) {
        res.push_back(cc);
      }
    }
  }
  return res;
}

static void parse_cells(Cell* dest, CellClass** src, std::size_t c,
                        const LTRBStruct& L) {
  for (std::size_t k = 0; k < c; k++) {
    auto* cc = src[k];
    const int ix = (L.Right + 1) * (cc->MapCoords.Y) + (cc->MapCoords.X);

    auto& C = dest[k];
    parse_Cell(&C, ix, *cc);
  }
}

static void update_modified_cells(
    const Cell* current, Cell* previous, std::size_t c,
    gpb::RepeatedPtrField<ra2yrproto::ra2yr::Cell>* difference) {
  for (std::size_t k = 0; k < c; k++) {
    auto& C = current[k];
    if (!serialize::bytes_equal(&C, &previous[k])) {
      C.copy_to(difference->Add(), &C);
      previous[k] = C;
    }
  }
}

template <int N>
static void apply_cell_stride(
    Cell* previous, Cell* cell_buf, CellClass** cells,
    gpb::RepeatedPtrField<ra2yrproto::ra2yr::Cell>* difference, MapClass* M) {
  const auto& L = M->MapCoordBounds;
  parse_cells(cell_buf, cells, N, L);
  if (!serialize::bytes_equal(cell_buf, previous, sizeof(Cell) * N)) {
    update_modified_cells(cell_buf, previous, N, difference);
  }
}

// TODO(shmocz): objects
void ra2::parse_map(
    std::vector<Cell>* previous, MapClass* D,
    gpb::RepeatedPtrField<ra2yrproto::ra2yr::Cell>* difference) {
  static constexpr int chunk = 128;
  static std::vector<CellClass*> valid_cell_objects;
  static std::array<Cell, chunk> cell_buf;

  // Initialize valid cells
  if (valid_cell_objects.empty()) {
    valid_cell_objects = get_valid_cells(D);
  }

  auto* cellbuf = cell_buf.data();
  auto* cells = valid_cell_objects.data();
  Cell* prev_cells = previous->data();

  const auto sz = static_cast<int>(valid_cell_objects.size());
  const auto cnt = sz / chunk;
  for (int i = 0; i < cnt * chunk; i += chunk) {
    apply_cell_stride<chunk>(&prev_cells[i], cellbuf, &cells[i], difference, D);
  }

  for (int i = cnt * chunk; i < sz; i += chunk) {
    const auto& L = D->MapCoordBounds;
    auto c = std::min(chunk, (sz - cnt * chunk));
    parse_cells(cellbuf, &cells[i], c, L);
    if (!(std::memcmp(cellbuf, &prev_cells[i], c * sizeof(Cell)) == 0)) {
      update_modified_cells(cellbuf, &prev_cells[i], c, difference);
    }
  }
}

// TODO(shmocz): save only the utilized cells
void ra2::parse_MapData(ra2yrproto::ra2yr::MapData* dst, MapClass* src,
                        ra2::abi::ABIGameMD* abi) {
  auto* M = src;
  auto L = M->MapCoordBounds;
  auto sz = (L.Right + 1) * (L.Bottom + 1);
  auto* map_data = dst;
  map_data->set_width(L.Right + 1);
  map_data->set_height(L.Bottom + 1);
  auto* m = map_data->mutable_cells();
  if (m->size() != sz) {
    m->Clear();
    for (int i = 0; i < sz; i++) {
      m->Add();
    }
  }

  for (int j = 0; j <= L.Bottom; j++) {
    for (int i = 0; i <= L.Right; i++) {
      CellStruct coords{static_cast<i16>(i), static_cast<i16>(j)};
      auto* src_cell = M->TryGetCellAt(coords);

      if (src_cell != nullptr) {
        auto& c = m->at((L.Right + 1) * j + i);
        c.set_land_type(
            static_cast<ra2yrproto::ra2yr::LandType>(src_cell->LandType));
        c.set_height(src_cell->Height);
        c.set_level(src_cell->Level);
        c.set_radiation_level(src_cell->RadLevel);
        c.set_overlay_data(src_cell->OverlayData);
        if (src_cell->FirstObject != nullptr) {
          c.mutable_objects()->Clear();
          auto* o = c.add_objects();
          o->set_pointer_self(reinterpret_cast<u32>(src_cell->FirstObject));
        }
        if (c.land_type() == ra2yrproto::ra2yr::LandType::LAND_TYPE_Tiberium) {
          c.set_tiberium_value(
              ra2::abi::CellClass_GetContainedTiberiumValue::call(
                  abi, reinterpret_cast<std::uintptr_t>(src_cell)));
        }
        c.set_shrouded(ra2::abi::CellClass_IsShrouded::call(abi, src_cell));
        c.set_passability(src_cell->Passability);
        c.set_wall_owner_index(src_cell->WallOwnerIndex);
        c.set_overlay_type_index(src_cell->OverlayTypeIndex);
      }
    }
  }
}

template <typename T>
static void parse_EventList(
    gpb::RepeatedPtrField<ra2yrproto::ra2yr::Event>* dst, T* list) {
  if (dst->size() != list->Count) {
    dst->Clear();

    for (auto i = 0; i < list->Count; i++) {
      (void)dst->Add();
    }
  }
  EventListUtil::apply(list, [dst](const EventEntry& e) {
    auto& it = dst->at(e.index);
    ra2::EventParser P(e.e, &it, e.timing);
    P.parse();
    return false;
  });
}

void ra2::parse_EventLists(ra2yrproto::ra2yr::GameState* G,
                           ra2yrproto::ra2yr::EventListsSnapshot* ES,
                           std::size_t max_size) {
  parse_EventList(G->mutable_out_list(), &EventClass::OutList.get());
  parse_EventList(G->mutable_do_list(), &EventClass::DoList.get());
  parse_EventList(G->mutable_megamission_list(),
                  &EventClass::MegaMissionList.get());
  auto* L = ES->mutable_lists();
  auto* F = ES->mutable_frame();

  // append
  ra2yrproto::ra2yr::EventLists EL;
  EL.mutable_out_list()->CopyFrom(G->out_list());
  EL.mutable_do_list()->CopyFrom(G->do_list());
  EL.mutable_megamission_list()->CopyFrom(G->megamission_list());
  // remove first if size exceeded
  if (L->size() >= static_cast<int>(max_size)) {
    L->erase(L->begin());
    F->erase(F->begin());
  }
  L->Add()->CopyFrom(EL);
  F->Add(G->current_frame());
}

void ra2::parse_prerequisiteGroups(ra2yrproto::ra2yr::PrerequisiteGroups* T) {
  auto& R = RulesClass::Instance;
  auto f = [](auto& L, auto* d) {
    auto IT = abi::DVCIterator(&L);
    for (auto id : IT) {
      d->Add(id);
    }
  };

  f(R->PrerequisiteProc, T->mutable_proc());
  f(R->PrerequisiteTech, T->mutable_tech());
  f(R->PrerequisiteRadar, T->mutable_radar());
  f(R->PrerequisiteBarracks, T->mutable_barracks());
  f(R->PrerequisiteFactory, T->mutable_factory());
  f(R->PrerequisitePower, T->mutable_power());
}

void ra2::parse_HouseClass(ra2yrproto::ra2yr::House* dst,
                           const HouseClass* src) {
  dst->set_array_index(src->ArrayIndex);
  dst->set_current_player(src->IsInPlayerControl);
  dst->set_defeated(src->Defeated);
  dst->set_is_game_over(src->IsGameOver);
  dst->set_is_loser(src->IsLoser);
  dst->set_is_winner(src->IsWinner);
  dst->set_money(src->Balance);
  dst->set_power_drain(src->PowerDrain);
  dst->set_power_output(src->PowerOutput);
  dst->set_start_credits(src->StartingCredits);
  dst->set_self(reinterpret_cast<std::uintptr_t>(src));
  dst->set_name(src->PlainName);
  dst->set_faction(src->Type->ID);
  dst->set_type_array_index(src->Type->ArrayIndex);
  dst->set_allied_infiltrated(src->Side0TechInfiltrated);
  dst->set_soviet_infiltrated(src->Side1TechInfiltrated);
  dst->set_third_infiltrated(src->Side2TechInfiltrated);
  dst->set_is_human_player(src->IsHumanPlayer);
}

void ra2::parse_Factories(
    gpb::RepeatedPtrField<ra2yrproto::ra2yr::Factory>* dst) {
  auto* D = FactoryClass::Array.get();
  if (dst->size() != D->Count) {
    ra2yrcpp::protocol::fill_repeated_empty(dst, D->Count);
  }

  for (int i = 0; i < D->Count; i++) {
    auto* I = D->Items[i];
    auto& O = dst->at(i);
    O.set_object(reinterpret_cast<u32>(I->Object));
    O.set_owner(reinterpret_cast<u32>(I->Owner));
    O.set_progress_timer(I->Production.Value);
    O.set_on_hold(I->OnHold);
    O.set_completed(O.progress_timer() == cfg::PRODUCTION_STEPS);
    auto A = ra2::abi::DVCIterator(&I->QueuedObjects);
    O.clear_queued_objects();
    for (auto* p : A) {
      O.add_queued_objects(reinterpret_cast<u32>(p));
    }
  }
}

gpb::RepeatedPtrField<ra2yrproto::ra2yr::ObjectTypeClass>*
ra2::parse_AbstractTypeClasses(
    gpb::RepeatedPtrField<ra2yrproto::ra2yr::ObjectTypeClass>* T,
    ra2::abi::ABIGameMD* abi) {
  auto [D, T_] = init_arrays<AbstractTypeClass>(T);

  for (int i = 0; i < D->Count; i++) {
    // TODO: UB?
    ra2::TypeClassParser P({abi, D->Items[i]}, &T->at(i));
    P.parse();
  }
  return T;
}

// Insert msg to repeated ptr field, resizing the field if necessary
template <typename T>
static T* try_get_message(gpb::RepeatedPtrField<T>* f, int index,
                          int capacity_increment = 10) {
  if (f->size() < (index + 1)) {
    ra2yrcpp::protocol::fill_repeated(f, capacity_increment);
  }
  return &f->at(index);
}

// TODO: const corr
template <typename T, typename D>
int parse_object_array(ra2::abi::ABIGameMD* abi, T* src,
                       gpb::RepeatedPtrField<D>* dst, int j) {
  ra2yrproto::ra2yr::Object* O = nullptr;
  for (int i = 0; i < src->Count; i++) {
    if (O == nullptr) {
      O = try_get_message(dst, j);
    }
    try {
      ra2::ClassParser P({abi, src->Items[i]}, O);
      P.parse();
      j++;
      O = nullptr;
    } catch (...) {
    }
  }
  return j;
}

void ra2::parse_Objects(ra2yrproto::ra2yr::GameState* G,
                        ra2::abi::ABIGameMD* abi) {
  auto* H = G->mutable_objects();
  auto techno = parse_object_array(abi, TechnoClass::Array.get(), H, 0);
  auto terrain = parse_object_array(abi, TerrainClass::Array.get(), H, techno);
  auto total = parse_object_array(abi, SuperClass::Array.get(), H, terrain);
  (void)ra2yrcpp::protocol::truncate(H,
                                     total);  // remove empty entries at the end
}

void ra2::parse_HouseClasses(ra2yrproto::ra2yr::GameState* G) {
  auto [D, H] = init_arrays<HouseClass>(G->mutable_houses());

  for (int i = 0; i < D->Count; i++) {
    ra2::parse_HouseClass(&H->at(i), D->Items[i]);
  }
}

void ra2::parse_RulesClass(ra2yrproto::ra2yr::GameState* G) {
  auto* D = RulesClass::Instance.get();
  auto* dst = G->mutable_rules_class();

  dst->Clear();

  dst->set_veteran_ratio(D->VeteranRatio);
  dst->set_veteran_combat(D->VeteranCombat);
  dst->set_veteran_speed(D->VeteranSpeed);
  dst->set_veteran_sight(D->VeteranSight);
  dst->set_veteran_armor(D->VeteranArmor);
  dst->set_veteran_rof(D->VeteranROF);

  dst->set_refund_percent(D->RefundPercent);
  dst->set_reload_rate(D->ReloadRate);
  dst->set_repair_percent(D->RepairPercent);
  dst->set_repair_rate(D->RepairRate);
  dst->set_repair_step(D->RepairStep);
  dst->set_u_repair_rate(D->URepairRate);
  dst->set_i_repair_rate(D->IRepairRate);
  dst->set_i_repair_step(D->IRepairStep);
  dst->set_self_heal_infantry_frames(D->SelfHealInfantryFrames);
  dst->set_self_heal_infantry_amount(D->SelfHealInfantryAmount);
  dst->set_self_heal_unit_frames(D->SelfHealUnitFrames);
  dst->set_self_heal_unit_amount(D->SelfHealUnitAmount);

  dst->set_multiple_factory(D->MultipleFactory);
  dst->set_min_low_power_production_speed(D->MinLowPowerProductionSpeed);
  dst->set_max_low_power_production_speed(D->MaxLowPowerProductionSpeed);

  dst->set_bridge_strength(D->BridgeStrength);

  dst->set_occupy_damage_multiplier(D->OccupyDamageMultiplier);
  dst->set_occupy_rof_multiplier(D->OccupyROFMultiplier);
  dst->set_occupy_weapon_range(D->OccupyWeaponRange);

  dst->set_bunker_damage_multiplier(
      *reinterpret_cast<const float* /* YRpp */>(&D->BunkerDamageMultiplier));
  dst->set_bunker_rof_multiplier(D->BunkerROFMultiplier);
  dst->set_bunker_weapon_range(D->BunkerWeaponRangeBonus);

  dst->set_open_topped_damage_multiplier(D->OpenToppedDamageMultiplier);
  dst->set_open_topped_range_bonus(D->OpenToppedRangeBonus);
  dst->set_open_topped_warp_distance(D->OpenToppedWarpDistance);

  dst->set_elevation_increment(D->ElevationIncrement);
  dst->set_elevation_increment_bonus(D->ElevationIncrementBonus);
  dst->set_elevation_bonus_cap(D->ElevationBonusCap);

  dst->set_flight_level(D->FlightLevel);

  dst->set_pointer_prerequisite_proc_alternate(reinterpret_cast<u32>(D->PrerequisiteProcAlternate));

  dst->set_harvester_load_rate(D->HarvesterLoadRate);
  dst->set_harvester_dump_rate(D->HarvesterDumpRate);
}

ra2yrproto::ra2yr::ObjectTypeClass* ra2::find_type_class(
    gpb::RepeatedPtrField<ra2yrproto::ra2yr::ObjectTypeClass>* types,
    ra2yrproto::ra2yr::AbstractType rtti_id, int array_index) {
  auto e = std::find_if(types->begin(), types->end(), [&](const auto& v) {
    return v.array_index() == array_index && v.type() == rtti_id;
  });
  return e != types->end() ? &(*e) : nullptr;
}

bool ra2::is_local(const gpb::RepeatedPtrField<ra2yrproto::ra2yr::House>& H) {
  return std::count_if(H.begin(), H.end(),
                       [](const auto& h) { return h.is_human_player(); }) == 1;
}
