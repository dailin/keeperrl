/* Copyright (C) 2013-2014 Michal Brzozowski (rusolis@poczta.fm)

   This file is part of KeeperRL.

   KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program.
   If not, see http://www.gnu.org/licenses/ . */

#include "stdafx.h"

#include "creature_factory.h"
#include "monster.h"
#include "level.h"
#include "entity_set.h"
#include "effect.h"
#include "item_factory.h"
#include "creature_attributes.h"
#include "view_object.h"
#include "view_id.h"
#include "creature.h"
#include "game.h"
#include "name_generator.h"
#include "player_message.h"
#include "equipment.h"
#include "minion_task_map.h"
#include "spell_map.h"
#include "tribe.h"
#include "monster_ai.h"
#include "sound.h"
#include "player.h"
#include "map_memory.h"
#include "body.h"
#include "attack_type.h"
#include "attack_level.h"
#include "attack.h"
#include "spell_map.h"
#include "item_type.h"
#include "item.h"
#include "spawn_type.h"
#include "furniture.h"
#include "experience_type.h"
#include "creature_debt.h"

SERIALIZE_DEF(CreatureFactory, tribe, creatures, weights, unique, tribeOverrides, levelIncrease)
SERIALIZATION_CONSTRUCTOR_IMPL(CreatureFactory);

CreatureFactory CreatureFactory::singleCreature(TribeId tribe, CreatureId id) {
  return CreatureFactory(tribe, {id}, {1}, {});
}

class BoulderController : public Monster {
  public:
  BoulderController(WCreature c, Vec2 dir) : Monster(c, MonsterAIFactory::idle()), direction(dir) {
    CHECK(direction.length4() == 1);
  }

  virtual void makeMove() override {
    Position nextPos = getCreature()->getPosition().plus(direction);
    if (WCreature c = nextPos.getCreature()) {
      if (!c->getBody().isKilledByBoulder()) {
        if (nextPos.canEnterEmpty(getCreature())) {
          getCreature()->swapPosition(direction);
          return;
        }
      } else {
        health -= c->getBody().getBoulderDamage();
        if (health <= 0) {
          nextPos.globalMessage(getCreature()->getName().the() + " crashes on " + c->getName().the(),
                "You hear a crash");
          getCreature()->dieNoReason();
          c->takeDamage(Attack(getCreature(), AttackLevel::MIDDLE, AttackType::HIT, 1000, 35, false));
          return;
        } else {
          c->you(MsgType::KILLED_BY, getCreature()->getName().the());
          c->dieWithAttacker(getCreature());
        }
      }
    }
    if (auto furniture = nextPos.getFurniture(FurnitureLayer::MIDDLE))
      if (furniture->canDestroy(getCreature()->getMovementType(), DestroyAction::Type::BOULDER) &&
          *furniture->getStrength(DestroyAction::Type::BOULDER) <
          health * getCreature()->getAttr(AttrType::STRENGTH)) {
        health -= *furniture->getStrength(DestroyAction::Type::BOULDER) /
            (double) getCreature()->getAttr(AttrType::STRENGTH);
        getCreature()->destroyImpl(direction, DestroyAction::Type::BOULDER);
      }
    if (auto action = getCreature()->move(direction))
      action.perform(getCreature());
    else {
      nextPos.globalMessage(getCreature()->getName().the() + " crashes on the " + nextPos.getName(),
          "You hear a crash");
      getCreature()->dieNoReason();
      return;
    }
    int speed = getCreature()->getAttr(AttrType::SPEED);
    double deceleration = 0.1;
    speed -= deceleration * 100 * 100 / speed;
    if (speed < 30 && !getCreature()->isDead()) {
      getCreature()->dieNoReason();
      return;
    }
    getCreature()->getAttributes().setBaseAttr(AttrType::SPEED, speed);
  }

  virtual void you(MsgType type, const string& param) override {
    string msg, msgNoSee;
    switch (type) {
      case MsgType::BURN: msg = getCreature()->getName().the() + " burns in the " + param; break;
      case MsgType::DROWN: msg = getCreature()->getName().the() + " falls into the " + param;
                           msgNoSee = "You hear a loud splash"; break;
      case MsgType::KILLED_BY: msg = getCreature()->getName().the() + " is destroyed by " + param; break;
      case MsgType::ENTER_PORTAL: msg = getCreature()->getName().the() + " disappears in the portal."; break;
      default: break;
    }
    if (!msg.empty())
      getCreature()->monsterMessage(msg, msgNoSee);
  }

  SERIALIZE_ALL(SUBCLASS(Monster), direction)
  SERIALIZATION_CONSTRUCTOR(BoulderController)

  private:
  Vec2 SERIAL(direction);
  double health = 1;
};

PCreature CreatureFactory::getRollingBoulder(TribeId tribe, Vec2 direction) {
  ViewObject viewObject(ViewId::BOULDER, ViewLayer::CREATURE);
  viewObject.setModifier(ViewObjectModifier::NO_UP_MOVEMENT);
  auto ret = makeOwner<Creature>(viewObject, tribe, CATTR(
            c.viewId = ViewId::BOULDER;
            c.attr[AttrType::DEXTERITY] = 1;
            c.attr[AttrType::STRENGTH] = 250;
            c.body = Body::nonHumanoid(Body::Material::ROCK, Body::Size::HUGE).setDeathSound(none);
            c.attr[AttrType::SPEED] = 140;
            c.permanentEffects[LastingEffect::BLIND] = 1;
            c.boulder = true;
            c.name = "boulder";
            ));
  ret->setController(makeOwner<BoulderController>(ret.get(), direction));
  return ret;
}

class SokobanController : public Monster {
  public:
  SokobanController(WCreature c) : Monster(c, MonsterAIFactory::idle()) {}

  virtual void onBump(WCreature player) override {
    Vec2 goDir = player->getPosition().getDir(getCreature()->getPosition());
    if (goDir.isCardinal4() && getCreature()->getPosition().plus(goDir).canEnter(
          getCreature()->getMovementType().setForced(true))) {
      getCreature()->displace(getCreature()->getLocalTime(), goDir);
      player->move(goDir).perform(player);
    }
  }

  virtual void you(MsgType type, const string& param) override {
    string msg, msgNoSee;
    switch (type) {
      case MsgType::BURN: msg = getCreature()->getName().the() + " burns in the " + param; break;
      case MsgType::DROWN: msg = getCreature()->getName().the() + " falls into the " + param;
                           msgNoSee = "You hear a loud splash"; break;
      case MsgType::KILLED_BY: msg = getCreature()->getName().the() + " is destroyed by " + param; break;
      case MsgType::ENTER_PORTAL: msg = getCreature()->getName().the() + " disappears in the portal."; break;
      default: break;
    }
    if (!msg.empty())
      getCreature()->monsterMessage(msg, msgNoSee);
  }

  SERIALIZE_ALL(SUBCLASS(Monster));
  SERIALIZATION_CONSTRUCTOR(SokobanController);

  private:
};

PCreature CreatureFactory::getSokobanBoulder(TribeId tribe) {
  ViewObject viewObject(ViewId::BOULDER, ViewLayer::CREATURE);
  viewObject.setModifier(ViewObjectModifier::NO_UP_MOVEMENT).setModifier(ViewObjectModifier::REMEMBER);
  auto ret = makeOwner<Creature>(viewObject, tribe, CATTR(
            c.viewId = ViewId::BOULDER;
            c.attr[AttrType::DEXTERITY] = 1;
            c.attr[AttrType::STRENGTH] = 1000;
            c.body = Body::nonHumanoid(Body::Material::ROCK, Body::Size::HUGE).setDeathSound(none);
            c.attr[AttrType::SPEED] = 140;
            c.permanentEffects[LastingEffect::BLIND] = 1;
            c.boulder = true;
            c.name = "boulder";));
  ret->setController(makeOwner<SokobanController>(ret.get()));
  return ret;
}

CreatureAttributes CreatureFactory::getKrakenAttributes(ViewId id, const char* name) {
  return CATTR(
      c.viewId = id;
      c.attr[AttrType::SPEED] = 40;
      c.body = Body::nonHumanoid(Body::Size::LARGE).setDeathSound(none);
      c.attr[AttrType::STRENGTH] = 15;
      c.attr[AttrType::DEXTERITY] = 15;
      c.barehandedDamage = 10;
      c.permanentEffects[LastingEffect::POISON_RESISTANT] = 1;
      c.skills.insert(SkillId::SWIMMING);
      c.name = name;);
}

ViewId CreatureFactory::getViewId(CreatureId id) {
  static EnumMap<CreatureId, ViewId> idMap([](CreatureId id) {
    auto c = fromId(id, TribeId::getMonster());
    return c->getViewObject().id();
  });
  return idMap[id];
}

class KrakenController : public Monster {
  public:
  KrakenController(WCreature c) : Monster(c, MonsterAIFactory::monster()) {
  }

  KrakenController(WCreature c, WeakPointer<KrakenController> f) : KrakenController(c) {
    father = f;
  }

  virtual bool isCustomController() override {
    return true;
  }

  int getMaxSpawns() {
    if (father)
      return 1;
    else
      return 7;
  }

  virtual void onKilled(WConstCreature attacker) override {
    if (attacker) {
      if (father)
        attacker->playerMessage("You cut the kraken's tentacle");
      else
        attacker->playerMessage("You kill the kraken!");
    }
    for (WCreature c : spawns)
      if (!c->isDead())
        c->dieNoReason();
  }

  virtual void you(MsgType type, const string& param) override {
    string msg, msgNoSee;
    switch (type) {
      case MsgType::KILLED_BY:
        if (father)
          msg = param + "cuts the kraken's tentacle";
        else
          msg = param + "kills the kraken!";
        break;
      case MsgType::DIE:
      case MsgType::DIE_OF: return;
      default: Monster::you(type, param); break;
    }
    if (!msg.empty())
      getCreature()->monsterMessage(msg, msgNoSee);
  }

  void pullEnemy(WCreature held) {
    held->you(MsgType::HAPPENS_TO, getCreature()->getName().the() + " pulls");
    if (father) {
      held->setHeld(father->getCreature());
      Vec2 pullDir = held->getPosition().getDir(getCreature()->getPosition());
      double localTime = getCreature()->getLocalTime();
      getCreature()->dieNoReason(Creature::DropType::NOTHING);
      held->displace(localTime, pullDir);
    } else {
      held->you(MsgType::ARE, "eaten by " + getCreature()->getName().the());
      held->dieNoReason();
    }
  }

  WCreature getHeld() {
    for (auto pos : getCreature()->getPosition().neighbors8())
      if (auto creature = pos.getCreature())
        if (creature->getHoldingCreature() == getCreature())
          return creature;
    return nullptr;
  }

  WCreature getVisibleEnemy() {
    const int radius = 10;
    WCreature ret = nullptr;
    auto myPos = getCreature()->getPosition();
    for (Position pos : getCreature()->getPosition().getRectangle(Rectangle::centered(Vec2(0, 0), radius)))
      if (WCreature c = pos.getCreature())
        if (c->getAttributes().getCreatureId() != getCreature()->getAttributes().getCreatureId() &&
            (!ret || ret->getPosition().dist8(myPos) > c->getPosition().dist8(myPos)) &&
            getCreature()->canSee(c) && getCreature()->isEnemy(c) && !c->getHoldingCreature())
          ret = c;
    return ret;
  }

  void considerAttacking(WCreature c) {
    auto pos = c->getPosition();
    Vec2 v = getCreature()->getPosition().getDir(pos);
    if (v.length8() == 1) {
      c->you(MsgType::HAPPENS_TO, getCreature()->getName().the() + " swings itself around");
      c->setHeld(getCreature());
    } else {
      pair<Vec2, Vec2> dirs = v.approxL1();
      vector<Vec2> moves;
      if (getCreature()->getPosition().plus(dirs.first).canEnter(
            {{MovementTrait::WALK, MovementTrait::SWIM}}))
        moves.push_back(dirs.first);
      if (getCreature()->getPosition().plus(dirs.second).canEnter(
            {{MovementTrait::WALK, MovementTrait::SWIM}}))
        moves.push_back(dirs.second);
      if (!moves.empty()) {
        Vec2 move = Random.choose(moves);
        ViewId viewId = getCreature()->getPosition().plus(move).canEnter({MovementTrait::SWIM})
          ? ViewId::KRAKEN_WATER : ViewId::KRAKEN_LAND;
        auto spawn = makeOwner<Creature>(getCreature()->getTribeId(),
              CreatureFactory::getKrakenAttributes(viewId, "kraken tentacle"));
        spawn->setController(makeOwner<KrakenController>(spawn.get(), getThis().dynamicCast<KrakenController>()));
        spawns.push_back(spawn.get());
        getCreature()->getPosition().plus(move).addCreature(std::move(spawn));
      }
    }
  }

  virtual void makeMove() override {
    for (WCreature c : spawns)
      if (c->isDead()) {
        spawns.removeElement(c);
        break;
      }
    if (spawns.empty()) {
      if (auto held = getHeld()) {
        pullEnemy(held);
        return;
      } else if (auto c = getVisibleEnemy()) {
        considerAttacking(c);
      } else if (father && Random.roll(5)) {
        getCreature()->dieNoReason(Creature::DropType::NOTHING);
        return;
      }
    }
    getCreature()->wait().perform(getCreature());
  }

  SERIALIZE_ALL(SUBCLASS(Monster), ready, spawns, father);
  SERIALIZATION_CONSTRUCTOR(KrakenController);

  private:
  bool SERIAL(ready) = false;
  vector<WCreature> SERIAL(spawns);
  WeakPointer<KrakenController> SERIAL(father);
};

class KamikazeController : public Monster {
  public:
  KamikazeController(WCreature c, MonsterAIFactory f) : Monster(c, f) {}

  virtual void makeMove() override {
    for (Position pos : getCreature()->getPosition().neighbors8())
      if (WCreature c = pos.getCreature())
        if (getCreature()->isEnemy(c) && getCreature()->canSee(c)) {
          getCreature()->monsterMessage(getCreature()->getName().the() + " explodes!");
          for (Position v : c->getPosition().neighbors8())
            v.fireDamage(1);
          c->getPosition().fireDamage(1);
          getCreature()->dieNoReason(Creature::DropType::ONLY_INVENTORY);
          return;
        }
    Monster::makeMove();
  }

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar & SUBCLASS(Monster);
  }

  SERIALIZATION_CONSTRUCTOR(KamikazeController);
};

class ShopkeeperController : public Monster, public EventListener<ShopkeeperController> {
  public:
  ShopkeeperController(WCreature c, Rectangle area)
      : Monster(c, MonsterAIFactory::stayInLocation(area)), shopArea(area) {
  }

  vector<Position> getAllShopPositions() const {
    return shopArea.getAllSquares().transform([this](Vec2 v){ return Position(v, myLevel); });
  }

  bool isShopPosition(const Position& pos) {
    return pos.isSameLevel(myLevel) && pos.getCoord().inRectangle(shopArea);
  }

  virtual void makeMove() override {
    if (firstMove) {
      myLevel = getCreature()->getLevel();
      subscribeTo(getCreature()->getPosition().getModel());
      for (Position v : getAllShopPositions()) {
        for (WItem item : v.getItems())
          item->setShopkeeper(getCreature());
        v.clearItemIndex(ItemIndex::FOR_SALE);
      }
      firstMove = false;
    }
    if (!getCreature()->getPosition().isSameLevel(myLevel)) {
      Monster::makeMove();
      return;
    }
    vector<Creature::Id> creatures;
    for (Position v : getAllShopPositions())
      if (WCreature c = v.getCreature()) {
        creatures.push_back(c->getUniqueId());
        if (!prevCreatures.contains(c) && !thieves.contains(c) && !getCreature()->isEnemy(c)) {
          if (!debtors.contains(c))
            c->playerMessage("\"Welcome to " + *getCreature()->getName().first() + "'s shop!\"");
          else {
            c->playerMessage("\"Pay your debt or... !\"");
            thiefCount.erase(c);
          }
        }
      }
    for (auto debtorId : copyOf(debtors))
      if (!creatures.contains(debtorId))
        for (auto pos : getCreature()->getPosition().getRectangle(Rectangle::centered(Vec2(0, 0), 30)))
          if (auto debtor = pos.getCreature())
            if (debtor->getUniqueId() == debtorId) {
              debtor->playerMessage("\"Come back, you owe me " + toString(debtor->getDebt().getAmountOwed(getCreature())) +
                  " gold!\"");
              if (++thiefCount.getOrInit(debtor) == 4) {
                debtor->playerMessage("\"Thief! Thief!\"");
                getCreature()->getTribe()->onItemsStolen(debtor);
                thiefCount.erase(debtor);
                debtors.erase(debtor);
                thieves.insert(debtor);
                for (WItem item : debtor->getEquipment().getItems())
                  item->setShopkeeper(nullptr);
                break;
              }
            }
    prevCreatures.clear();
    for (auto c : creatures)
      prevCreatures.insert(c);
    Monster::makeMove();
  }

  virtual void onItemsGiven(vector<WItem> items, WCreature from) override {
    int paid = items.filter(Item::classPredicate(ItemClass::GOLD)).size();
    from->getDebt().add(getCreature(), -paid);
    if (from->getDebt().getAmountOwed(getCreature()) <= 0)
      debtors.erase(from);
  }
  
  void onEvent(const GameEvent& event) {
    switch (event.getId()) {
      case EventId::ITEMS_APPEARED: {
          auto info = event.get<EventInfo::ItemsAppeared>();
          if (isShopPosition(info.position)) {
           for (auto& it : info.items) {
             it->setShopkeeper(getCreature());
             info.position.clearItemIndex(ItemIndex::FOR_SALE);
           }
         }
        }
        break;
      case EventId::PICKED_UP: {
          auto info = event.get<EventInfo::ItemsHandled>();
          if (isShopPosition(info.creature->getPosition())) {
            for (auto& item : info.items)
              if (item->isShopkeeper(getCreature())) {
                info.creature->getDebt().add(getCreature(), item->getPrice());
                debtors.insert(info.creature);
              }
          }
        }
        break;
      case EventId::DROPPED: {
          auto info = event.get<EventInfo::ItemsHandled>();
          if (isShopPosition(info.creature->getPosition())) {
            for (auto& item : info.items)
              if (item->isShopkeeper(getCreature())) {
                info.creature->getDebt().add(getCreature(), -item->getPrice());
                if (info.creature->getDebt().getAmountOwed(getCreature()) == 0)
                  debtors.erase(info.creature);
              }
          }
        }
        break;
      default:
        break;
    }
  }

  template <class Archive>
  void serialize(Archive& ar, const unsigned int) {
    ar & SUBCLASS(Monster) & SUBCLASS(EventListener);
    ar(prevCreatures, debtors, thiefCount, thieves, shopArea, myLevel, firstMove);
  }

  SERIALIZATION_CONSTRUCTOR(ShopkeeperController);

  private:
  EntitySet<Creature> SERIAL(prevCreatures);
  EntitySet<Creature> SERIAL(debtors);
  EntityMap<Creature, int> SERIAL(thiefCount);
  EntitySet<Creature> SERIAL(thieves);
  Rectangle SERIAL(shopArea);
  WLevel SERIAL(myLevel) = nullptr;
  bool SERIAL(firstMove) = true;
};

PCreature CreatureFactory::addInventory(PCreature c, const vector<ItemType>& items) {
  for (ItemType item : items) {
    PItem it = ItemFactory::fromId(item);
    c->take(std::move(it));
  }
  return c;
}

PCreature CreatureFactory::getShopkeeper(Rectangle shopArea, TribeId tribe) {
  auto ret = makeOwner<Creature>(tribe,
      CATTR(
        c.viewId = ViewId::SHOPKEEPER;
        c.attr[AttrType::SPEED] = 100;
        c.body = Body::humanoid(Body::Size::LARGE);
        c.attr[AttrType::STRENGTH] = 17;
        c.attr[AttrType::DEXTERITY] = 13;
        c.barehandedDamage = 13;
        c.chatReactionFriendly = "complains about high import tax"_s;
        c.chatReactionHostile = "\"Die!\""_s;
        c.name = "shopkeeper";
        c.name->setFirst(NameGenerator::get(NameGeneratorId::FIRST_MALE)->getNext());));
  ret->setController(makeOwner<ShopkeeperController>(ret.get(), shopArea));
  vector<ItemType> inventory(Random.get(100, 300), ItemId::GOLD_PIECE);
  inventory.push_back(ItemId::SWORD);
  inventory.push_back(ItemId::LEATHER_ARMOR);
  inventory.push_back(ItemId::LEATHER_BOOTS);
  inventory.push_back({ItemId::POTION, EffectId::HEAL});
  inventory.push_back({ItemId::POTION, EffectId::HEAL});
  return addInventory(std::move(ret), inventory);
}

class IllusionController : public DoNothingController {
  public:
  IllusionController(WCreature c, double deathT) : DoNothingController(c), deathTime(deathT) {}

  void kill() {
    getCreature()->monsterMessage("The illusion disappears.");
    if (!getCreature()->isDead())
      getCreature()->dieNoReason();
  }

  virtual void onBump(WCreature c) override {
    c->attack(getCreature(), none, false).perform(c);
    kill();
  }

  virtual void makeMove() override {
    if (getCreature()->getGlobalTime() >= deathTime)
      kill();
    else
      getCreature()->wait().perform(getCreature());
  }

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& SUBCLASS(DoNothingController);
    ar(deathTime);
  }

  SERIALIZATION_CONSTRUCTOR(IllusionController);

  private:
  double SERIAL(deathTime);
};

PCreature CreatureFactory::getIllusion(WCreature creature) {
  ViewObject viewObject(creature->getViewObject().id(), ViewLayer::CREATURE, "Illusion");
  viewObject.setModifier(ViewObject::Modifier::ILLUSION);
  auto ret = makeOwner<Creature>(viewObject, creature->getTribeId(), CATTR(
          c.viewId = ViewId::ROCK; //overriden anyway
          c.illusionViewObject = creature->getViewObject();
          (*c.illusionViewObject)->removeModifier(ViewObject::Modifier::INVISIBLE);
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoidSpirit(Body::Size::LARGE).setDeathSound(SoundId::MISSED_ATTACK);
          c.attr[AttrType::STRENGTH] = 1;
          c.attr[AttrType::DEXTERITY] = 1;
          c.barehandedDamage = 20; // just so it's not ignored by creatures
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.noAttackSound = true;
          c.name = "illusion";));
  ret->setController(makeOwner<IllusionController>(ret.get(), creature->getGlobalTime() + Random.get(5, 10)));
  return ret;
}

REGISTER_TYPE(BoulderController);
REGISTER_TYPE(SokobanController);
REGISTER_TYPE(KrakenController);
REGISTER_TYPE(KamikazeController);
REGISTER_TYPE(ShopkeeperController);
REGISTER_TYPE(IllusionController);
REGISTER_TYPE(ListenerTemplate<ShopkeeperController>)

TribeId CreatureFactory::getTribeFor(CreatureId id) {
  if (auto t = tribeOverrides[id])
    return *t;
  else
    return *tribe;
}

PCreature CreatureFactory::random() {
  return random(MonsterAIFactory::monster());
}

PCreature CreatureFactory::random(const MonsterAIFactory& actorFactory) {
  CreatureId id;
  if (unique.size() > 0) {
    id = unique.back();
    unique.pop_back();
  } else
    id = Random.choose(creatures, weights);
  PCreature ret = fromId(id, getTribeFor(id), actorFactory);
  ret->getAttributes().increaseBaseExpLevel(levelIncrease);
  return ret;
}

PCreature CreatureFactory::get(const CreatureAttributes& attr, TribeId tribe, const ControllerFactory& factory) {
  auto ret = makeOwner<Creature>(tribe, attr);
  ret->setController(factory.get(ret.get()));
  return ret;
}

CreatureFactory& CreatureFactory::increaseLevel(double l) {
  levelIncrease += l;
  return *this;
}

CreatureFactory::CreatureFactory(TribeId t, const vector<CreatureId>& c, const vector<double>& w,
    const vector<CreatureId>& u, EnumMap<CreatureId, optional<TribeId>> overrides)
    : tribe(t), creatures(c), weights(w), unique(u), tribeOverrides(overrides) {
}

CreatureFactory::CreatureFactory(const vector<tuple<CreatureId, double, TribeId>>& c, const vector<CreatureId>& u)
    : unique(u) {
  for (auto& elem : c) {
    creatures.push_back(std::get<0>(elem));
    weights.push_back(std::get<1>(elem));
    tribeOverrides[std::get<0>(elem)] = std::get<2>(elem);
  }
}

CreatureFactory CreatureFactory::humanVillage(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::KNIGHT, CreatureId::ARCHER,
      CreatureId::PESEANT, CreatureId::CHILD, CreatureId::HORSE, CreatureId::DONKEY, CreatureId::COW,
      CreatureId::PIG, CreatureId::DOG },
      { 2, 6, 6, 4, 1, 1, 1, 1, 6}, {CreatureId::KNIGHT});
}

CreatureFactory CreatureFactory::humanPeaceful(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::PESEANT,
      CreatureId::CHILD, CreatureId::HORSE, CreatureId::DONKEY, CreatureId::COW, CreatureId::PIG, CreatureId::DOG },
      { 2, 1, 1, 1, 1, 1, 1}, {});
}

CreatureFactory CreatureFactory::tutorialVillage(TribeId tribe) {
  return CreatureFactory(tribe, {}, {}, {CreatureId::PESEANT, CreatureId::PESEANT, CreatureId::PESEANT,
      CreatureId::PESEANT, CreatureId::PESEANT, CreatureId::DONKEY, CreatureId::PIG, CreatureId::PIG, CreatureId::DOG});
}

CreatureFactory CreatureFactory::gnomeVillage(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::GNOME },
      { 1}, { CreatureId::GNOME_CHIEF});
}

CreatureFactory CreatureFactory::gnomeEntrance(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::GNOME }, {1});
}

CreatureFactory CreatureFactory::koboldVillage(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::KOBOLD }, {1});
}

CreatureFactory CreatureFactory::darkElfVillage(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::DARK_ELF, CreatureId::DARK_ELF_CHILD, CreatureId::DARK_ELF_WARRIOR },
      { 1, 1, 2}, { CreatureId::DARK_ELF_LORD});
}

CreatureFactory CreatureFactory::darkElfEntrance(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::DARK_ELF_WARRIOR }, {1});
}

CreatureFactory CreatureFactory::humanCastle(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::KNIGHT, CreatureId::ARCHER,
      CreatureId::PESEANT, CreatureId::CHILD, CreatureId::HORSE, CreatureId::DONKEY, CreatureId::COW,
      CreatureId::PIG, CreatureId::DOG },
      { 10, 6, 2, 1, 1, 1, 1, 1, 1}, {CreatureId::AVATAR});
}

static optional<pair<CreatureFactory, CreatureFactory>> splashFactories;

void CreatureFactory::initSplash(TribeId tribe) {
  splashFactories = Random.choose(
      make_pair(CreatureFactory(tribe, { CreatureId::KNIGHT, CreatureId::ARCHER}, { 1, 1}, {}),
        CreatureFactory::singleType(tribe, CreatureId::AVATAR)),
      make_pair(CreatureFactory(tribe, { CreatureId::WARRIOR}, { 1}, {}),
        CreatureFactory::singleType(tribe, CreatureId::SHAMAN)),
      make_pair(CreatureFactory(tribe, { CreatureId::ELF_ARCHER}, { 1}, {}),
        CreatureFactory::singleType(tribe, CreatureId::ELF_LORD)),
      make_pair(CreatureFactory(tribe, { CreatureId::DWARF}, { 1}, {}),
        CreatureFactory::singleType(tribe, CreatureId::DWARF_BARON)),
      make_pair(CreatureFactory(tribe, { CreatureId::LIZARDMAN}, { 1}, {}),
        CreatureFactory::singleType(tribe, CreatureId::LIZARDLORD))
      );
}

CreatureFactory CreatureFactory::splashHeroes(TribeId tribe) {
  if (!splashFactories)
    initSplash(tribe);
  return splashFactories->first;
}

CreatureFactory CreatureFactory::splashLeader(TribeId tribe) {
  if (!splashFactories)
    initSplash(tribe);
  return splashFactories->second;
}

CreatureFactory CreatureFactory::splashMonsters(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::GNOME, CreatureId::GOBLIN, CreatureId::OGRE,
      CreatureId::SPECIAL_HLBN, CreatureId::SPECIAL_BLBW, CreatureId::WOLF, CreatureId::CAVE_BEAR,
      CreatureId::BAT, CreatureId::WEREWOLF, CreatureId::ZOMBIE, CreatureId::VAMPIRE, CreatureId::DOPPLEGANGER,
      CreatureId::SUCCUBUS},
      { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {}, {}).increaseLevel(25);
}

CreatureFactory CreatureFactory::elvenVillage(TribeId tribe) {
  double armedRatio = 0.4;
  CreatureFactory ret(tribe, { CreatureId::ELF, CreatureId::ELF_CHILD, CreatureId::HORSE,
      CreatureId::COW, CreatureId::DOG },
      { 2, 2, 1, 1, 0}, {});
  double sum = 0;
  for (double w : ret.weights)
    sum += w;
  ret.weights.push_back(sum * armedRatio / (1 - armedRatio));
  ret.creatures.push_back(CreatureId::ELF_ARCHER);
  ret.unique.push_back(CreatureId::ELF_LORD);
  return ret;
}

CreatureFactory CreatureFactory::elvenCottage(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::ELF, CreatureId::ELF_CHILD, CreatureId::HORSE,
      CreatureId::COW, CreatureId::DOG },
      { 2, 2, 1, 1, 0}, {CreatureId::ELF_ARCHER});
}

CreatureFactory CreatureFactory::forrest(TribeId tribe) {
  return CreatureFactory(tribe,
      { CreatureId::DEER, CreatureId::FOX, CreatureId::BOAR },
      { 4, 2, 2}, {});
}

CreatureFactory CreatureFactory::crypt(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::ZOMBIE}, { 1}, {});
}

CreatureFactory CreatureFactory::vikingTown(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::WARRIOR}, { 1}, {CreatureId::SHAMAN});
}

CreatureFactory CreatureFactory::lizardTown(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::LIZARDMAN, }, { 1}, {CreatureId::LIZARDLORD});
}

CreatureFactory CreatureFactory::dwarfTown(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::DWARF, CreatureId::DWARF_FEMALE}, { 2, 1},{ CreatureId::DWARF_BARON});
}

CreatureFactory CreatureFactory::dwarfCave(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::DWARF, CreatureId::DWARF_FEMALE}, { 2, 1});
}

CreatureFactory CreatureFactory::antNest(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::ANT_WORKER, CreatureId::ANT_SOLDIER}, { 2, 1},
      { CreatureId::ANT_QUEEN});
}

CreatureFactory CreatureFactory::orcTown(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::ORC, CreatureId::OGRE }, {1, 1});
}

CreatureFactory CreatureFactory::insects(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::SPIDER}, {1});
}

CreatureFactory CreatureFactory::waterCreatures(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::KRAKEN }, {1});
}

CreatureFactory CreatureFactory::elementals(TribeId tribe) {
  return CreatureFactory(tribe, {CreatureId::AIR_ELEMENTAL, CreatureId::FIRE_ELEMENTAL, CreatureId::WATER_ELEMENTAL,
      CreatureId::EARTH_ELEMENTAL}, {1, 1, 1, 1}, {});
}

CreatureFactory CreatureFactory::lavaCreatures(TribeId tribe) {
  return CreatureFactory(tribe, { CreatureId::FIRE_SPHERE }, {1}, { });
}

CreatureFactory CreatureFactory::singleType(TribeId tribe, CreatureId id) {
  return CreatureFactory(tribe, { id}, {1}, {});
}

CreatureFactory CreatureFactory::gnomishMines(TribeId peaceful, TribeId enemy, int level) {
  return CreatureFactory({
      make_tuple(CreatureId::BANDIT, 100., enemy),
      make_tuple(CreatureId::GREEN_DRAGON, 5., enemy),
      make_tuple(CreatureId::RED_DRAGON, 5., enemy),
      make_tuple(CreatureId::SOFT_MONSTER, 5., enemy),
      make_tuple(CreatureId::CYCLOPS, 15., enemy),
      make_tuple(CreatureId::WITCH, 15., enemy),
      make_tuple(CreatureId::CLAY_GOLEM, 20., enemy),
      make_tuple(CreatureId::STONE_GOLEM, 20., enemy),
      make_tuple(CreatureId::IRON_GOLEM, 20., enemy),
      make_tuple(CreatureId::LAVA_GOLEM, 20., enemy),
      make_tuple(CreatureId::FIRE_ELEMENTAL, 10., enemy),
      make_tuple(CreatureId::WATER_ELEMENTAL, 10., enemy),
      make_tuple(CreatureId::EARTH_ELEMENTAL, 10., enemy),
      make_tuple(CreatureId::AIR_ELEMENTAL, 10., enemy),
      make_tuple(CreatureId::GNOME, 100., peaceful),
      make_tuple(CreatureId::GNOME_CHIEF, 20., peaceful),
      make_tuple(CreatureId::DWARF, 100., enemy),
      make_tuple(CreatureId::DWARF_FEMALE, 40., enemy),
      make_tuple(CreatureId::JACKAL, 200., enemy),
      make_tuple(CreatureId::BAT, 200., enemy),
      make_tuple(CreatureId::SNAKE, 150., enemy),
      make_tuple(CreatureId::SPIDER, 200., enemy),
      make_tuple(CreatureId::FLY, 100., enemy),
      make_tuple(CreatureId::RAT, 100., enemy)});
}

static ViewId getSpecialViewId(bool humanoid, bool large, bool body, bool wings) {
  static vector<ViewId> specialViewIds {
    ViewId::SPECIAL_BLBN,
    ViewId::SPECIAL_BLBW,
    ViewId::SPECIAL_BLGN,
    ViewId::SPECIAL_BLGW,
    ViewId::SPECIAL_BMBN,
    ViewId::SPECIAL_BMBW,
    ViewId::SPECIAL_BMGN,
    ViewId::SPECIAL_BMGW,
    ViewId::SPECIAL_HLBN,
    ViewId::SPECIAL_HLBW,
    ViewId::SPECIAL_HLGN,
    ViewId::SPECIAL_HLGW,
    ViewId::SPECIAL_HMBN,
    ViewId::SPECIAL_HMBW,
    ViewId::SPECIAL_HMGN,
    ViewId::SPECIAL_HMGW,
  };
  return specialViewIds[humanoid * 8 + (!large) * 4 + (!body) * 2 + wings];
}

static string getSpeciesName(bool humanoid, bool large, bool body, bool wings) {
  static vector<string> names {
    "devitablex",
    "owlbeast",
    "hellar dra",
    "marilisk",
    "gelaticorn",
    "mant eatur",
    "phanticore",
    "yeth horro",
    "yeth amon",
    "mantic dra",
    "unic cread",
    "under hulk",
    "nightshasa",
    "manananggal",
    "dire spawn",
    "shamander",
  };
  return names[humanoid * 8 + (!large) * 4 + (!body) * 2 + wings];
}

static optional<EffectType> getSpecialBeastAttack(bool large, bool body, bool wings) {
  static vector<optional<EffectType>> attacks {
    none,
    EffectType(EffectId::FIRE),
    EffectType(EffectId::FIRE),
    none,
    EffectType(EffectId::LASTING, LastingEffect::POISON),
    none,
    EffectType(EffectId::LASTING, LastingEffect::POISON),
    none,
  };
  return attacks[(!large) * 4 + (!body) * 2 + wings];
}

static EnumMap<BodyPart, int> getSpecialBeastBody(bool large, bool living, bool wings) {
  static vector<EnumMap<BodyPart, int>> parts {
    {
      { BodyPart::LEG, 2}},
    {
      { BodyPart::ARM, 2},
      { BodyPart::LEG, 2},
      { BodyPart::WING, 2},
      { BodyPart::HEAD, 1}},
    {
      { BodyPart::LEG, 4},
      { BodyPart::HEAD, 1}},
    {
      { BodyPart::ARM, 2},
      { BodyPart::WING, 2},
      { BodyPart::HEAD, 1}},
    {},
    { 
      { BodyPart::LEG, 2},
      { BodyPart::WING, 2},
      { BodyPart::HEAD, 1}},
    {
      { BodyPart::LEG, 8},
      { BodyPart::HEAD, 1}},
    { 
      { BodyPart::WING, 2},
      { BodyPart::HEAD, 1}},
  };
  return parts[(!large) * 4 + (!living) * 2 + wings];
}

PCreature CreatureFactory::getSpecial(TribeId tribe, bool humanoid, bool large, bool living, bool wings,
    const ControllerFactory& factory) {
  Body body = Body(humanoid, living ? Body::Material::FLESH : Body::Material::SPIRIT,
      large ? Body::Size::LARGE : Body::Size::MEDIUM);
  if (wings)
    body.addWings();
  string name = getSpeciesName(humanoid, large, living, wings);
  PCreature c = get(CATTR(
        c.viewId = getSpecialViewId(humanoid, large, living, wings);
        c.isSpecial = true;
        c.body = body;
        c.attr[AttrType::SPEED] = Random.get(80, 120);
        if (!large)
          c.attr[AttrType::SPEED] += 20;
        c.attr[AttrType::STRENGTH] = Random.get(18, 24);
        c.attr[AttrType::DEXTERITY] = Random.get(18, 24);
        if (large) {
          c.attr[AttrType::STRENGTH] += 6;
          c.attr[AttrType::DEXTERITY] -= 2;
        }
        c.barehandedDamage = Random.get(5, 15);
        c.spawnType = humanoid ? SpawnType::HUMANOID : SpawnType::BEAST;
        if (humanoid) {
          c.skills.setValue(SkillId::WEAPON_MELEE, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::UNARMED_MELEE, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::ARCHERY, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::SORCERY, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::MANA, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::WORKSHOP, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::FORGE, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::LABORATORY, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::JEWELER, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::FURNACE, Random.getDouble(0, 1));
        }
        if (humanoid) {
          c.chatReactionFriendly = "\"I am the mighty " + name + "\"";
          c.chatReactionHostile = "\"I am the mighty " + name + ". Die!\"";
          c.minionTasks.setValue(MinionTask::CRAFT, 1);
          c.minionTasks.setValue(MinionTask::TRAIN, 4); 
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.minionTasks.setValue(MinionTask::STUDY, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
        } else {
          c.chatReactionFriendly = c.chatReactionHostile = "The " + name + " snarls.";
        }
        c.name = name;
        c.name->setStack(humanoid ? "legendary humanoid" : "legendary beast");
        c.name->setFirst(NameGenerator::get(NameGeneratorId::DEMON)->getNext());
        if (!humanoid) {
          c.body->setBodyParts(getSpecialBeastBody(large, living, wings));
          c.attr[AttrType::STRENGTH] += 5;
          c.attr[AttrType::DEXTERITY] += 5;
          c.barehandedDamage += 5;
          c.attackEffect = getSpecialBeastAttack(large, living, wings);
        }
        if (Random.roll(3))
          c.skills.insert(SkillId::SWIMMING);
        ), tribe, factory);
  if (body.isHumanoid()) {
    if (Random.roll(4)) {
      c->take(ItemFactory::fromId(ItemId::BOW));
      c->take(ItemFactory::fromId(ItemId::ARROW, Random.get(20, 36)));
    }
    c->take(ItemFactory::fromId(Random.choose(
            ItemId::SPECIAL_SWORD, ItemId::SPECIAL_BATTLE_AXE, ItemId::SPECIAL_WAR_HAMMER)));
  }
  return c;
}

CreatureAttributes CreatureFactory::getAttributes(CreatureId id) {
  return getAttributesFromId(id).setCreatureId(id);
}

CreatureAttributes CreatureFactory::getAttributesFromId(CreatureId id) {
  switch (id) {
    case CreatureId::KEEPER: 
      return CATTR(
          c.viewId = ViewId::KEEPER;
          c.retiredViewId = ViewId::RETIRED_KEEPER;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 15;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 5;
          c.name = "Keeper";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::FIRST_MALE)->getNext());
          c.name->useFullTitle();
          c.spells->add(SpellId::HEALING);
          c.minionTasks.setValue(MinionTask::STUDY, 1);
          c.minionTasks.setValue(MinionTask::CRAFT, 0.0001);
          c.minionTasks.setValue(MinionTask::TRAIN, 0.0001); 
          c.minionTasks.setValue(MinionTask::THRONE, 0.0001); 
          c.skills.setValue(SkillId::SORCERY, 0.2);
          c.skills.setValue(SkillId::LABORATORY, 0.2);
          c.skills.setValue(SkillId::MANA, 1);
      );
    case CreatureId::KEEPER_F:
      return CATTR(
          c.viewId = ViewId::KEEPER_F;
          c.retiredViewId = ViewId::RETIRED_KEEPER_F;
          c.attr[AttrType::SPEED] = 100;
          c.gender = Gender::female;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 15;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 5;
          c.name = "Keeper";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::FIRST_FEMALE)->getNext());
          c.name->useFullTitle();
          c.spells->add(SpellId::HEALING);
          c.minionTasks.setValue(MinionTask::STUDY, 1);
          c.minionTasks.setValue(MinionTask::CRAFT, 0.0001);
          c.minionTasks.setValue(MinionTask::TRAIN, 0.0001);
          c.minionTasks.setValue(MinionTask::THRONE, 0.0001);
          c.skills.setValue(SkillId::SORCERY, 0.2);
          c.skills.setValue(SkillId::LABORATORY, 0.2);
          c.skills.setValue(SkillId::MANA, 1);
      );
    case CreatureId::ADVENTURER:
      return CATTR(
          c.viewId = ViewId::PLAYER;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 15;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 5;
          c.maxExpFromCombat = 15;
          c.name = "Adventurer";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::FIRST_MALE)->getNext());
          c.name->useFullTitle();
          c.skills.insert(SkillId::AMBUSH);
      );
    case CreatureId::ADVENTURER_F:
      return CATTR(
          c.viewId = ViewId::PLAYER_F;
          c.gender = Gender::female;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 15;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 5;
          c.maxExpFromCombat = 15;
          c.name = "Adventurer";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::FIRST_FEMALE)->getNext());
          c.name->useFullTitle();
      );
    case CreatureId::BANDIT:
      return CATTR(
          c.viewId = ViewId::BANDIT;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 15;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all law enforcement"_s;
          c.chatReactionHostile = "\"Die!\""_s;
 //         c.skills.insert(SkillId::DISARM_TRAPS);
          c.name = "bandit";);
    case CreatureId::GHOST: 
      return CATTR(
          c.viewId = ViewId::GHOST;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::nonHumanoidSpirit(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 14;
          c.attr[AttrType::DEXTERITY] = 35;
          c.barehandedDamage = 3;
          c.barehandedAttack = AttackType::HIT;
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.chatReactionFriendly = "\"Wouuuouuu!!!\""_s;
          c.chatReactionHostile = "\"Wouuuouuu!!!\""_s;
          c.name = "ghost";);
    case CreatureId::SPIRIT:
      return CATTR(
          c.viewId = ViewId::SPIRIT;
          c.attr[AttrType::STRENGTH] = 24;
          c.attr[AttrType::SPEED] = 100;
          c.attr[AttrType::DEXTERITY] = 30;
          c.barehandedDamage = 30;
          c.body = Body::nonHumanoidSpirit(Body::Size::LARGE);
          c.barehandedAttack = AttackType::HIT;
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.chatReactionFriendly = "\"Wouuuouuu!!!\""_s;
          c.chatReactionHostile = "\"Wouuuouuu!!!\""_s;
          c.name = "ancient spirit";);
    case CreatureId::LOST_SOUL:
      return CATTR(
          c.viewId = ViewId::GHOST;
          c.attr[AttrType::STRENGTH] = 5;
          c.attr[AttrType::DEXTERITY] = 35;
          c.attr[AttrType::SPEED] = 80;
          c.courage = 10;
          c.spawnType = SpawnType::DEMON;
          c.barehandedAttack = AttackType::POSSESS;
          c.minionTasks.setValue(MinionTask::RITUAL, 1);
          c.body = Body::nonHumanoidSpirit(Body::Size::LARGE);
          c.barehandedDamage = 3;
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.chatReactionFriendly = "\"Wouuuouuu!!!\""_s;
          c.chatReactionHostile = "\"Wouuuouuu!!!\""_s;
          c.name = "ghost";);
    case CreatureId::SUCCUBUS:
      return CATTR(
          c.attr[AttrType::STRENGTH] = 5;
          c.attr[AttrType::SPEED] = 80;
          c.attr[AttrType::DEXTERITY] = 35;
          c.barehandedDamage = 3;
          c.barehandedAttack = AttackType::HIT;
          c.viewId = ViewId::SUCCUBUS;
          c.spawnType = SpawnType::DEMON;
          c.body = Body::humanoidSpirit(Body::Size::LARGE).addWings();
          c.minionTasks.setValue(MinionTask::COPULATE, 1);
          c.minionTasks.setValue(MinionTask::RITUAL, 1);
          c.gender = Gender::female;
          c.courage = 0.0;
          c.name = CreatureName("succubus", "succubi");
          );
    case CreatureId::DOPPLEGANGER:
      return CATTR(
          c.viewId = ViewId::DOPPLEGANGER;
          c.attr[AttrType::SPEED] = 80;
          c.attr[AttrType::STRENGTH] = 14;
          c.attr[AttrType::DEXTERITY] = 35;
          c.barehandedDamage = 3;
          c.barehandedAttack = AttackType::HIT;
          c.body = Body::nonHumanoidSpirit(Body::Size::LARGE);
          c.minionTasks.setValue(MinionTask::RITUAL, 1);
          c.spawnType = SpawnType::DEMON;
          c.skills.insert(SkillId::CONSUMPTION);
          c.name = "doppelganger";
          );
    case CreatureId::WITCH: 
      return CATTR(
          c.viewId = ViewId::WITCH;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 14;
          c.attr[AttrType::DEXTERITY] = 12;
          c.name = CreatureName("witch", "witches");
          c.name->setFirst("Cornelia");
          c.barehandedDamage = 6;
          c.gender = Gender::female;
          c.chatReactionFriendly = "curses all humans"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          );
    case CreatureId::WITCHMAN: 
      return CATTR(
          c.viewId = ViewId::WITCHMAN;
          c.attr[AttrType::SPEED] = 140;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 20;
          c.attr[AttrType::DEXTERITY] = 20;
          c.name = CreatureName("witchman", "witchmen");
          c.name->setFirst(NameGenerator::get(NameGeneratorId::FIRST_MALE)->getNext());
          c.barehandedDamage = 6;
          c.gender = Gender::male;
          c.chatReactionFriendly = "curses all monsters"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          );
    case CreatureId::CYCLOPS: 
      return CATTR(
          c.viewId = ViewId::CYCLOPS;
          c.attr[AttrType::SPEED] = 90;
          c.body = Body::humanoid(Body::Size::LARGE).setWeight(400);
          c.attr[AttrType::STRENGTH] = 33;
          c.attr[AttrType::DEXTERITY] = 23;
          c.barehandedDamage = 10;
          c.barehandedAttack = AttackType::BITE;
          c.name = CreatureName("cyclops", "cyclopes");
          c.name->setFirst(NameGenerator::get(NameGeneratorId::CYCLOPS)->getNext());
          );
    case CreatureId::MINOTAUR: 
      return CATTR(
          c.viewId = ViewId::MINOTAUR;
          c.attr[AttrType::SPEED] = 90;
          c.body = Body::humanoid(Body::Size::LARGE).setWeight(400);
          c.attr[AttrType::STRENGTH] = 45;
          c.attr[AttrType::DEXTERITY] = 35;
          c.barehandedDamage = 20;
          c.barehandedAttack = AttackType::BITE;
          c.name = "minotaur";);
    case CreatureId::SOFT_MONSTER:
      return CATTR(
          c.viewId = ViewId::SOFT_MONSTER;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE).setWeight(400);
          c.attr[AttrType::STRENGTH] = 45;
          c.attr[AttrType::DEXTERITY] = 35;
          c.barehandedDamage = 1;
          c.courage = 0.1;
          c.name = "soft monster";);
    case CreatureId::HYDRA:
      return CATTR(
          c.viewId = ViewId::HYDRA;
          c.attr[AttrType::SPEED] = 110;
          c.body = Body::nonHumanoid(Body::Size::LARGE).setWeight(400);
          c.attr[AttrType::STRENGTH] = 35;
          c.attr[AttrType::DEXTERITY] = 45;
          c.attackEffect = EffectType(EffectId::LASTING, LastingEffect::POISON);
          c.permanentEffects[LastingEffect::POISON_RESISTANT] = 1;
          c.barehandedDamage = 10;
          c.barehandedAttack = AttackType::BITE;
          c.skills.insert(SkillId::SWIMMING);
          c.name = "hydra";);
    case CreatureId::SHELOB:
      return CATTR(
          c.viewId = ViewId::SHELOB;
          c.attr[AttrType::SPEED] = 110;
          c.body = Body::nonHumanoid(Body::Size::LARGE).setWeight(400)
              .setBodyParts({{BodyPart::LEG, 8}, {BodyPart::TORSO, 1}})
              .setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 35;
          c.attr[AttrType::DEXTERITY] = 45;
          c.attackEffect = EffectType(EffectId::LASTING, LastingEffect::POISON);
          c.permanentEffects[LastingEffect::POISON_RESISTANT] = 1;
          c.barehandedDamage = 10;
          c.barehandedAttack = AttackType::BITE;
          c.minionTasks.setValue(MinionTask::SPIDER, 1); 
          c.name = "giant spider";
          );
    case CreatureId::GREEN_DRAGON: 
      return CATTR(
          c.viewId = ViewId::GREEN_DRAGON;
          c.attr[AttrType::SPEED] = 90;
          c.body = Body::nonHumanoid(Body::Size::HUGE).setHorseBodyParts().addWings();
          c.attr[AttrType::STRENGTH] = 38;
          c.attr[AttrType::DEXTERITY] = 28;
          c.barehandedDamage = 10;
          c.barehandedAttack = AttackType::BITE;
          c.permanentEffects[LastingEffect::POISON_RESISTANT] = 1;
          c.name = "green dragon";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::DRAGON)->getNext());
          c.name->setStack("dragon");
          );
    case CreatureId::RED_DRAGON:
      return CATTR(
          c.viewId = ViewId::RED_DRAGON;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoid(Body::Size::HUGE).setHorseBodyParts().addWings();
          c.attr[AttrType::STRENGTH] = 47;
          c.attr[AttrType::DEXTERITY] = 28;
          c.barehandedDamage = 10;
          c.barehandedAttack = AttackType::BITE;
          c.name = "red dragon";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::DRAGON)->getNext());
          c.name->setStack("dragon");
          );
    case CreatureId::KNIGHT: 
      return CATTR(
          c.viewId = ViewId::KNIGHT;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 30;
          c.attr[AttrType::DEXTERITY] = 22;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.skills.setValue(SkillId::WEAPON_MELEE, 0.3);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "knight";);
    case CreatureId::CASTLE_GUARD: 
      return CATTR(
          c.viewId = ViewId::CASTLE_GUARD;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 30;
          c.attr[AttrType::DEXTERITY] = 22;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.skills.setValue(SkillId::WEAPON_MELEE, 0.3);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "guard";);
    case CreatureId::AVATAR: 
      return CATTR(
          c.viewId = ViewId::DUKE;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 40;
          c.attr[AttrType::DEXTERITY] = 29;
          c.barehandedDamage = 8;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.skills.setValue(SkillId::WEAPON_MELEE, 0.3);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.courage = 3;
          c.name = "Duke of " + NameGenerator::get(NameGeneratorId::WORLD)->getNext(););
    case CreatureId::WARRIOR:
      return CATTR(
          c.viewId = ViewId::WARRIOR;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 27;
          c.attr[AttrType::DEXTERITY] = 19;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.skills.setValue(SkillId::WEAPON_MELEE, 0.3);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "warrior";);
    case CreatureId::SHAMAN:
      return CATTR(
          c.viewId = ViewId::SHAMAN;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 27;
          c.attr[AttrType::DEXTERITY] = 19;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.skills.setValue(SkillId::WEAPON_MELEE, 0.3);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.courage = 3;
          c.spells->add(SpellId::HEALING);
          c.spells->add(SpellId::SPEED_SELF);
          c.spells->add(SpellId::STR_BONUS);
          c.spells->add(SpellId::SUMMON_SPIRIT);
          c.spells->add(SpellId::STUN_RAY);
          c.spells->add(SpellId::BLAST);
          c.skills.setValue(SkillId::SORCERY, 1);
          c.skills.insert(SkillId::HEALING);
          c.name = "shaman";);
    case CreatureId::ARCHER: 
      return CATTR(
          c.viewId = ViewId::ARCHER;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 17;
          c.attr[AttrType::DEXTERITY] = 24;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.skills.setValue(SkillId::ARCHERY, 1);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "archer";);
    case CreatureId::PESEANT: 
      return CATTR(
          c.viewId = Random.choose(ViewId::PESEANT, ViewId::PESEANT_WOMAN);
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 14;
          c.attr[AttrType::DEXTERITY] = 12;
          c.barehandedDamage = 3;
          c.innocent = true;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Heeelp!\""_s;
          c.minionTasks.setValue(MinionTask::CROPS, 4);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "peasant";);
    case CreatureId::CHILD: 
      return CATTR(
          c.viewId = ViewId::CHILD;
          c.attr[AttrType::SPEED] = 140;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 8;
          c.attr[AttrType::DEXTERITY] = 16;
          c.barehandedDamage = 3;
          c.innocent = true;
          c.chatReactionFriendly = "\"plaaaaay!\""_s;
          c.chatReactionHostile = "\"Heeelp!\""_s;
          c.minionTasks.setValue(MinionTask::CROPS, 4);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = CreatureName("child", "children"););
    case CreatureId::CLAY_GOLEM:
      return CATTR(
          c.viewId = ViewId::CLAY_GOLEM;
          c.attr[AttrType::SPEED] = 50;
          c.body = Body::nonHumanoid(Body::Material::CLAY, Body::Size::LARGE).setHumanoidBodyParts();
          c.attr[AttrType::STRENGTH] = 14;
          c.attr[AttrType::DEXTERITY] = 12;
          c.barehandedDamage = 13;
          c.barehandedAttack = AttackType::PUNCH;
          c.name = "clay golem";);
    case CreatureId::STONE_GOLEM: 
      return CATTR(
          c.viewId = ViewId::STONE_GOLEM;
          c.attr[AttrType::SPEED] = 60;
          c.body = Body::nonHumanoid(Body::Material::ROCK, Body::Size::LARGE).setHumanoidBodyParts();
          c.attr[AttrType::STRENGTH] = 16;
          c.attr[AttrType::DEXTERITY] = 14;
          c.barehandedDamage = 15;
          c.barehandedAttack = AttackType::PUNCH;
          c.name = "stone golem";);
    case CreatureId::IRON_GOLEM: 
      return CATTR(
          c.viewId = ViewId::IRON_GOLEM;
          c.attr[AttrType::SPEED] = 70;
          c.body = Body::nonHumanoid(Body::Material::IRON, Body::Size::LARGE).setHumanoidBodyParts();
          c.attr[AttrType::STRENGTH] = 18;
          c.attr[AttrType::DEXTERITY] = 16;
          c.barehandedDamage = 17;
          c.barehandedAttack = AttackType::PUNCH;
          c.name = "iron golem";);
    case CreatureId::LAVA_GOLEM: 
      return CATTR(
          c.viewId = ViewId::LAVA_GOLEM;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::nonHumanoid(Body::Material::LAVA, Body::Size::LARGE).setHumanoidBodyParts();
          c.attr[AttrType::STRENGTH] = 20;
          c.attr[AttrType::DEXTERITY] = 18;
          c.barehandedDamage = 19;
          c.barehandedAttack = AttackType::PUNCH;
          c.attackEffect = EffectId::FIRE;
          c.name = "lava golem";);
    case CreatureId::AUTOMATON: 
      return CATTR(
          c.viewId = ViewId::AUTOMATON;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoid(Body::Material::IRON, Body::Size::LARGE).setHumanoidBodyParts();
          c.attr[AttrType::STRENGTH] = 45;
          c.attr[AttrType::DEXTERITY] = 23;
          c.barehandedDamage = 15;
          c.barehandedAttack = AttackType::PUNCH;
          c.name = "automaton";);
    case CreatureId::ZOMBIE: 
      return CATTR(
          c.viewId = ViewId::ZOMBIE;
          c.attr[AttrType::SPEED] = 60;
          c.body = Body::humanoid(Body::Material::UNDEAD_FLESH, Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 14;
          c.attr[AttrType::DEXTERITY] = 13;
          c.barehandedDamage = 13;
          c.spawnType = SpawnType::UNDEAD;
          c.minionTasks.setValue(MinionTask::TRAIN, 4); 
          c.minionTasks.setValue(MinionTask::GRAVE, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.name = "zombie";);
    case CreatureId::SKELETON: 
      return CATTR(
          c.viewId = ViewId::SKELETON;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Material::BONE, Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 14;
          c.attr[AttrType::DEXTERITY] = 13;
          c.barehandedDamage = 13;
          c.name = "skeleton";);
    case CreatureId::VAMPIRE: 
      return CATTR(
          c.viewId = ViewId::VAMPIRE;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Material::UNDEAD_FLESH, Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 17;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 2;
          c.chatReactionFriendly = "\"All men be cursed!\""_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spawnType = SpawnType::UNDEAD;
          c.skills.insert(SkillId::NIGHT_VISION);
          c.minionTasks.setValue(MinionTask::TRAIN, 4); 
          c.minionTasks.setValue(MinionTask::STUDY, 1);
          c.minionTasks.setValue(MinionTask::GRAVE, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.skills.setValue(SkillId::SORCERY, 0.1);
          c.skills.setValue(SkillId::MANA, 0.4);
          c.name = "vampire";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::VAMPIRE)->getNext());
          );
    case CreatureId::VAMPIRE_LORD: 
      return CATTR(
          c.viewId = ViewId::VAMPIRE_LORD;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::humanoid(Body::Material::UNDEAD_FLESH, Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 20;
          c.attr[AttrType::DEXTERITY] = 27;
          c.barehandedDamage = 6;
          c.spawnType = SpawnType::UNDEAD;
          c.skills.insert(SkillId::NIGHT_VISION);
          c.skills.setValue(SkillId::SORCERY, 0.5);
          c.skills.setValue(SkillId::MANA, 0.7);
          c.minionTasks.setValue(MinionTask::TRAIN, 4); 
          c.minionTasks.setValue(MinionTask::STUDY, 1);
          c.minionTasks.setValue(MinionTask::GRAVE, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.name = "vampire lord";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::VAMPIRE)->getNext());
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.permanentEffects[LastingEffect::FIRE_RESISTANT] = 1;
          c.permanentEffects[LastingEffect::DARKNESS_SOURCE] = 1;
          for (SpellId id : Random.chooseN(Random.get(3, 6), {SpellId::WORD_OF_POWER, SpellId::DEX_BONUS,
              SpellId::STR_BONUS, SpellId::MAGIC_SHIELD, SpellId::STUN_RAY, SpellId::DECEPTION, SpellId::DECEPTION,
              SpellId::TELEPORT}))
            c.spells->add(id);
          c.chatReactionFriendly = c.chatReactionHostile =
              "\"There are times when you simply cannot refuse a drink!\""_s;
          );
    case CreatureId::MUMMY: 
      return CATTR(
          c.viewId = ViewId::MUMMY;
          c.attr[AttrType::SPEED] = 60;
          c.body = Body::humanoid(Body::Material::UNDEAD_FLESH, Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 12;
          c.attr[AttrType::DEXTERITY] = 13;
          c.barehandedDamage = 13;
          c.spawnType = SpawnType::UNDEAD;
          c.minionTasks.setValue(MinionTask::TRAIN, 4); 
          c.minionTasks.setValue(MinionTask::GRAVE, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.name = CreatureName("mummy", "mummies"););
    case CreatureId::ORC:
      return CATTR(
          c.viewId = ViewId::ORC;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 16;
          c.attr[AttrType::DEXTERITY] = 14;
          c.barehandedDamage = 3;
          c.spawnType = SpawnType::HUMANOID;
          c.chatReactionFriendly = "curses all elves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.minionTasks.setValue(MinionTask::TRAIN, 4);
          c.minionTasks.setValue(MinionTask::CRAFT, 1);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.skills.setValue(SkillId::WEAPON_MELEE, 0.3);
          c.skills.setValue(SkillId::WORKSHOP, 0.3);
          c.skills.setValue(SkillId::FORGE, 0.3);
          c.name = "orc";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::ORC)->getNext());
          );
    case CreatureId::ORC_SHAMAN:
      return CATTR(
          c.viewId = ViewId::ORC_SHAMAN;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 13;
          c.attr[AttrType::DEXTERITY] = 11;
          c.barehandedDamage = 3;
          c.spawnType = SpawnType::HUMANOID;
          c.minionTasks.setValue(MinionTask::CRAFT, 4);
          c.minionTasks.setValue(MinionTask::STUDY, 4);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.skills.setValue(SkillId::SORCERY, 0.7);
          c.skills.setValue(SkillId::MANA, 0.4);
          c.skills.setValue(SkillId::LABORATORY, 0.7);
          c.skills.insert(SkillId::HEALING);
          c.chatReactionFriendly = "curses all elves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.name = "orc shaman";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::ORC)->getNext());
          );
    case CreatureId::HARPY:
      return CATTR(
          c.viewId = ViewId::HARPY;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::humanoid(Body::Size::LARGE).addWings();
          c.attr[AttrType::STRENGTH] = 13;
          c.attr[AttrType::DEXTERITY] = 16;
          c.barehandedDamage = 3;
          c.spawnType = SpawnType::HUMANOID;
          c.minionTasks.setValue(MinionTask::TRAIN, 4);
          c.minionTasks.setValue(MinionTask::CRAFT, 0.5);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.skills.setValue(SkillId::LABORATORY, 0.3);
          c.gender = Gender::female;
          c.skills.setValue(SkillId::ARCHERY, 1);
          c.name = CreatureName("harpy", "harpies");
          c.name->setFirst(NameGenerator::get(NameGeneratorId::ORC)->getNext());
          );
    case CreatureId::KOBOLD: 
      return CATTR(
          c.viewId = ViewId::KOBOLD;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 12;
          c.attr[AttrType::DEXTERITY] = 13;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "talks about digging"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "kobold";);
    case CreatureId::GNOME: 
      return CATTR(
          c.viewId = ViewId::GNOME;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 12;
          c.attr[AttrType::DEXTERITY] = 13;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "talks about digging"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "gnome";);
    case CreatureId::GNOME_CHIEF:
      return CATTR(
          c.viewId = ViewId::GNOME_BOSS;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 15;
          c.attr[AttrType::DEXTERITY] = 16;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "talks about digging"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "gnome chief";);
    case CreatureId::GOBLIN: 
      return CATTR(
          c.viewId = ViewId::GOBLIN;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 12;
          c.attr[AttrType::DEXTERITY] = 13;
          c.barehandedDamage = 3;
          c.spawnType = SpawnType::HUMANOID;
          c.chatReactionFriendly = "talks about crafting"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.minionTasks.setValue(MinionTask::TRAIN, 1);
          c.minionTasks.setValue(MinionTask::CRAFT, 4);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.skills.insert(SkillId::DISARM_TRAPS);
          c.skills.setValue(SkillId::LABORATORY, 0.3);
          c.skills.setValue(SkillId::WORKSHOP, 0.9);
          c.skills.setValue(SkillId::FORGE, 0.9);
          c.skills.setValue(SkillId::JEWELER, 0.9);
          c.skills.setValue(SkillId::FURNACE, 0.9);
          c.name = "goblin";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::ORC)->getNext());
          );
    case CreatureId::IMP: 
      return CATTR(
          c.viewId = ViewId::IMP;
          c.attr[AttrType::SPEED] = 200;
          c.body = Body::humanoid(Body::Size::SMALL);
          c.attr[AttrType::STRENGTH] = 8;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 3;
          c.courage = 0.1;
          c.carryAnything = true;
          c.noChase = true;
          c.cantEquip = true;
          c.skills.insert(SkillId::CONSTRUCTION);
          c.chatReactionFriendly = "talks about digging"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.permanentEffects[LastingEffect::POISON_RESISTANT] = 1;
          c.name = "imp";);
    case CreatureId::PRISONER:
      return CATTR(
          c.viewId = ViewId::PRISONER;
          c.attr[AttrType::SPEED] = 100;
          c.attr[AttrType::STRENGTH] = 8;
          c.attr[AttrType::DEXTERITY] = 15;
          c.body = Body::humanoid(Body::Size::LARGE).setWeight(60);
          c.courage = 0.1;
          c.carryAnything = true;
          c.noChase = true;
          c.cantEquip = true;
          c.skills.insert(SkillId::CONSTRUCTION);
          c.chatReactionFriendly = "talks about escape plans"_s;
          c.minionTasks.setValue(MinionTask::PRISON, 1);
          c.minionTasks.setValue(MinionTask::BE_TORTURED, 0.0001);
          c.name = "prisoner";);
    case CreatureId::OGRE: 
      return CATTR(
          c.viewId = ViewId::OGRE;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::LARGE).setWeight(140);
          c.attr[AttrType::STRENGTH] = 20;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 6;
          c.name = "ogre";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::ORC)->getNext());
          c.spawnType = SpawnType::HUMANOID;
          c.minionTasks.setValue(MinionTask::CRAFT, 1);
          c.minionTasks.setValue(MinionTask::TRAIN, 4); 
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.minionTasks.setValue(MinionTask::EAT, 5);
          c.skills.setValue(SkillId::WEAPON_MELEE, 0.3);
          c.skills.setValue(SkillId::WORKSHOP, 0.5);
          c.skills.setValue(SkillId::FORGE, 0.5);
          c.skills.setValue(SkillId::FURNACE, 0.9);
          );
    case CreatureId::CHICKEN: 
      return CATTR(
          c.viewId = ViewId::CHICKEN;
          c.attr[AttrType::SPEED] = 50;
          c.body = Body::nonHumanoid(Body::Size::SMALL).setWeight(3).setMinionFood();
          c.attr[AttrType::STRENGTH] = 2;
          c.attr[AttrType::DEXTERITY] = 2;
          c.barehandedDamage = 5;
          c.name = "chicken";);
    case CreatureId::LEPRECHAUN: 
      return CATTR(
          c.viewId = ViewId::LEPRECHAUN;
          c.attr[AttrType::SPEED] = 160;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 10;
          c.attr[AttrType::DEXTERITY] = 16;
          c.barehandedDamage = 3;
          c.courage = 20;
          c.skills.insert(SkillId::STEALING);
          c.chatReactionFriendly = "discusses the weather"_s;
          c.chatReactionHostile = "discusses the weather"_s;
          c.name = "leprechaun";);
    case CreatureId::DWARF: 
      return CATTR(
          c.viewId = ViewId::DWARF;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::MEDIUM).setWeight(90);
          c.name = CreatureName("dwarf", "dwarves");
          c.name->setFirst(NameGenerator::get(NameGeneratorId::DWARF)->getNext());
          c.attr[AttrType::STRENGTH] = 28;
          c.attr[AttrType::DEXTERITY] = 19;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all orcs"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          );
    case CreatureId::DWARF_FEMALE:
      return CATTR(
          c.viewId = ViewId::DWARF_FEMALE;
          c.innocent = true;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::MEDIUM).setWeight(90);
          c.name = CreatureName("dwarf", "dwarves");
          c.name->setFirst(NameGenerator::get(NameGeneratorId::DWARF)->getNext());
          c.attr[AttrType::STRENGTH] = 25;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all orcs"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.gender = Gender::female;);
    case CreatureId::DWARF_BARON: 
      return CATTR(
          c.viewId = ViewId::DWARF_BARON;
          c.attr[AttrType::SPEED] = 90;
          c.body = Body::humanoid(Body::Size::MEDIUM).setWeight(120);
          c.attr[AttrType::STRENGTH] = 37;
          c.attr[AttrType::DEXTERITY] = 27;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all orcs"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.courage = 3;
          c.name = "dwarf baron";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::DWARF)->getNext());
          );
    case CreatureId::LIZARDMAN: 
      return CATTR(
          c.viewId = ViewId::LIZARDMAN;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 16;
          c.attr[AttrType::DEXTERITY] = 18;
          c.barehandedDamage = 9;
          c.barehandedAttack = AttackType::BITE;
          c.attackEffect = EffectType(EffectId::LASTING, LastingEffect::POISON);
          c.permanentEffects[LastingEffect::POISON_RESISTANT] = 1;
          c.chatReactionFriendly = "curses all humans"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = CreatureName("lizardman", "lizardmen"););
    case CreatureId::LIZARDLORD: 
      return CATTR(
          c.viewId = ViewId::LIZARDLORD;
          c.attr[AttrType::SPEED] = 140;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 24;
          c.attr[AttrType::DEXTERITY] = 28;
          c.barehandedDamage = 14;
          c.barehandedAttack = AttackType::BITE;
          c.attackEffect = EffectType(EffectId::LASTING, LastingEffect::POISON);
          c.permanentEffects[LastingEffect::POISON_RESISTANT] = 1;
          c.chatReactionFriendly = "curses all humans"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.courage = 3;
          c.name = "lizardman chief";);
    case CreatureId::ELF: 
      return CATTR(
          c.viewId = Random.choose(ViewId::ELF, ViewId::ELF_WOMAN);
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 11;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 3;
          c.innocent = true;
          c.chatReactionFriendly = "curses all dwarves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.insert(SkillId::ELF_VISION);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = CreatureName("elf", "elves"););
    case CreatureId::ELF_ARCHER: 
      return CATTR(
          c.viewId = ViewId::ELF_ARCHER;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 11;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dwarves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.setValue(SkillId::ARCHERY, 1);
          c.skills.insert(SkillId::ELF_VISION);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "elven archer";);
    case CreatureId::ELF_CHILD: 
      return CATTR(
          c.viewId = ViewId::ELF_CHILD;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::humanoid(Body::Size::SMALL);
          c.attr[AttrType::STRENGTH] = 7;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 0;
          c.innocent = true;
          c.chatReactionFriendly = "curses all dwarves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.insert(SkillId::ELF_VISION);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = CreatureName("elf child", "elf children"););
    case CreatureId::ELF_LORD: 
      return CATTR(
          c.viewId = ViewId::ELF_LORD;
          c.attr[AttrType::SPEED] = 140;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 16;
          c.attr[AttrType::DEXTERITY] = 23;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dwarves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.setValue(SkillId::ARCHERY, 1);
          c.skills.setValue(SkillId::WEAPON_MELEE, 1);
          c.skills.setValue(SkillId::SORCERY, 1);
          c.skills.insert(SkillId::HEALING);
          c.skills.insert(SkillId::ELF_VISION);
          c.spells->add(SpellId::HEALING);
          c.spells->add(SpellId::SPEED_SELF);
          c.spells->add(SpellId::STR_BONUS);
          c.spells->add(SpellId::STUN_RAY);
          c.spells->add(SpellId::BLAST);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "elf lord";);
    case CreatureId::DARK_ELF:
      return CATTR(
          c.viewId = Random.choose(ViewId::DARK_ELF, ViewId::DARK_ELF_WOMAN);
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 11;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 3;
          c.innocent = true;
          c.chatReactionFriendly = "curses all dwarves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.insert(SkillId::NIGHT_VISION);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = CreatureName("dark elf", "dark elves"););
    case CreatureId::DARK_ELF_WARRIOR:
      return CATTR(
          c.viewId = ViewId::DARK_ELF_WARRIOR;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 11;
          c.attr[AttrType::STRENGTH] = 11;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dwarves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.setValue(SkillId::ARCHERY, 0.5);
          c.skills.insert(SkillId::NIGHT_VISION);
          c.skills.setValue(SkillId::WEAPON_MELEE, 1);
          c.skills.setValue(SkillId::MANA, 0.3);
          c.skills.setValue(SkillId::SORCERY, 0.3);
          c.minionTasks.setValue(MinionTask::TRAIN, 4); 
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.minionTasks.setValue(MinionTask::STUDY, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.name = CreatureName("dark elf", "dark elves"););
    case CreatureId::DARK_ELF_CHILD:
      return CATTR(
          c.viewId = ViewId::DARK_ELF_CHILD;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::humanoid(Body::Size::SMALL);
          c.attr[AttrType::STRENGTH] = 7;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 0;
          c.innocent = true;
          c.chatReactionFriendly = "curses all dwarves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.insert(SkillId::NIGHT_VISION);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = CreatureName("dark elf child", "dark elf children"););
    case CreatureId::DARK_ELF_LORD:
      return CATTR(
          c.viewId = ViewId::DARK_ELF_LORD;
          c.attr[AttrType::SPEED] = 140;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 16;
          c.attr[AttrType::DEXTERITY] = 23;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dwarves"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.setValue(SkillId::ARCHERY, 1);
          c.skills.setValue(SkillId::WEAPON_MELEE, 1);
          c.skills.setValue(SkillId::SORCERY, 1);
          c.skills.insert(SkillId::HEALING);
          c.skills.insert(SkillId::NIGHT_VISION);
          c.spells->add(SpellId::HEALING);
          c.spells->add(SpellId::SPEED_SELF);
          c.spells->add(SpellId::STR_BONUS);
          c.spells->add(SpellId::STUN_RAY);
          c.spells->add(SpellId::BLAST);
          c.minionTasks.setValue(MinionTask::SLEEP, 1);
          c.name = "dark elf lord";);
    case CreatureId::DRIAD: 
      return CATTR(
          c.viewId = ViewId::DRIAD;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::humanoid(Body::Size::MEDIUM);
          c.attr[AttrType::STRENGTH] = 11;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all humans"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.spells->add(SpellId::HEALING);
          c.skills.insert(SkillId::ELF_VISION);
          c.skills.setValue(SkillId::ARCHERY, 1);
          c.name = "driad";);
    case CreatureId::HORSE: 
      return CATTR(
          c.viewId = ViewId::HORSE;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoid(Body::Size::LARGE).setWeight(500).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 10;
          c.attr[AttrType::DEXTERITY] = 13;
          c.innocent = true;
          c.animal = true;
          c.noChase = true;
          c.name = "horse";);
    case CreatureId::COW: 
      return CATTR(
          c.viewId = ViewId::COW;
          c.attr[AttrType::SPEED] = 40;
          c.body = Body::nonHumanoid(Body::Size::LARGE).setWeight(400).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 10;
          c.attr[AttrType::STRENGTH] = 10;
          c.attr[AttrType::DEXTERITY] = 12;
          c.innocent = true;
          c.animal = true;
          c.noChase = true;
          c.name = "cow";);
    case CreatureId::DONKEY: 
      return CATTR(
          c.viewId = ViewId::DONKEY;
          c.attr[AttrType::SPEED] = 40;
          c.body = Body::nonHumanoid(Body::Size::LARGE).setWeight(200).setHorseBodyParts()
              .setDeathSound(SoundId::DYING_DONKEY);
          c.attr[AttrType::STRENGTH] = 10;
          c.attr[AttrType::DEXTERITY] = 12;
          c.innocent = true;
          c.animal = true;
          c.noChase = true;
          c.name = "donkey";);
    case CreatureId::PIG: 
      return CATTR(
          c.viewId = ViewId::PIG;
          c.attr[AttrType::SPEED] = 60;
          c.body = Body::nonHumanoid(Body::Size::MEDIUM).setWeight(150).setHorseBodyParts().setMinionFood()
              .setDeathSound(SoundId::DYING_PIG);
          c.attr[AttrType::STRENGTH] = 12;
          c.attr[AttrType::DEXTERITY] = 8;
          c.innocent = true;
          c.noChase = true;
          c.animal = true;
          c.name = "pig";);
    case CreatureId::GOAT:
      return CATTR(
          c.viewId = ViewId::GOAT;
          c.attr[AttrType::SPEED] = 60;
          c.body = Body::nonHumanoid(Body::Size::MEDIUM).setHorseBodyParts().setMinionFood();
          c.attr[AttrType::STRENGTH] = 12;
          c.attr[AttrType::DEXTERITY] = 8;
          c.innocent = true;
          c.noChase = true;
          c.animal = true;
          c.name = "goat";);
    case CreatureId::JACKAL: 
      return CATTR(
          c.viewId = ViewId::JACKAL;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::nonHumanoid(Body::Size::SMALL).setWeight(10).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 10;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 2;
          c.animal = true;
          c.name = "jackal";);
    case CreatureId::DEER: 
      return CATTR(
          c.viewId = ViewId::DEER;
          c.attr[AttrType::SPEED] = 200;
          c.body = Body::nonHumanoid(Body::Size::LARGE).setWeight(400).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 10;
          c.attr[AttrType::DEXTERITY] = 17;
          c.innocent = true;
          c.animal = true;
          c.noChase = true;
          c.name = CreatureName("deer", "deer"););
    case CreatureId::BOAR: 
      return CATTR(
          c.viewId = ViewId::BOAR;
          c.attr[AttrType::SPEED] = 180;
          c.body = Body::nonHumanoid(Body::Size::MEDIUM).setWeight(200).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 18;
          c.attr[AttrType::DEXTERITY] = 15;
          c.innocent = true;
          c.animal = true;
          c.noChase = true;
          c.name = "boar";);
    case CreatureId::FOX: 
      return CATTR(
          c.viewId = ViewId::FOX;
          c.attr[AttrType::SPEED] = 140;
          c.body = Body::nonHumanoid(Body::Size::SMALL).setWeight(10).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 10;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 2;
          c.innocent = true;
          c.animal = true;
          c.noChase = true;
          c.name = CreatureName("fox", "foxes"););
    case CreatureId::CAVE_BEAR: 
      return CATTR(
          c.viewId = ViewId::BEAR;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::nonHumanoid(Body::Size::MEDIUM).setWeight(250).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 23;
          c.attr[AttrType::DEXTERITY] = 18;
          c.barehandedDamage = 11;
          c.animal = true;
          c.spawnType = SpawnType::BEAST;
          c.minionTasks.setValue(MinionTask::EXPLORE_CAVES, 1);
          c.minionTasks.setValue(MinionTask::LAIR, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.name = "cave bear";);
    case CreatureId::RAT: 
      return CATTR(
          c.viewId = ViewId::RAT;
          c.attr[AttrType::SPEED] = 180;
          c.body = Body::nonHumanoid(Body::Size::SMALL).setWeight(1).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 2;
          c.attr[AttrType::DEXTERITY] = 12;
          c.barehandedDamage = 2;
          c.animal = true;
          c.noChase = true;
          c.skills.insert(SkillId::SWIMMING);
          c.name = "rat";);
    case CreatureId::SPIDER: 
      return CATTR(
          c.viewId = ViewId::SPIDER;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoid(Body::Size::SMALL)
              .setWeight(0.3)
              .setBodyParts({{BodyPart::LEG, 8}, {BodyPart::TORSO, 1}})
              .setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 9;
          c.attr[AttrType::DEXTERITY] = 13;
          c.barehandedDamage = 10;
          c.attackEffect = EffectType(EffectId::LASTING, LastingEffect::POISON);
          c.animal = true;
          c.name = "spider";);
    case CreatureId::FLY: 
      return CATTR(
          c.viewId = ViewId::FLY;
          c.attr[AttrType::SPEED] = 150;
          c.body = Body::nonHumanoid(Body::Size::SMALL)
              .setWeight(0.1)
              .setBodyParts({{BodyPart::LEG, 6}, {BodyPart::WING, 2}, {BodyPart::TORSO, 1}})
              .setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 1;
          c.attr[AttrType::DEXTERITY] = 14;
          c.barehandedDamage = 10;
          c.courage = 100;
          c.noChase = true;
          c.animal = true;
          c.name = CreatureName("fly", "flies"););
    case CreatureId::ANT_WORKER:
      return CATTR(
          c.viewId = ViewId::ANT_WORKER;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoid(Body::Size::MEDIUM)
              .setWeight(10)
              .setBodyParts({{BodyPart::LEG, 6}, {BodyPart::HEAD, 1}, {BodyPart::TORSO, 1}})
              .setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 16;
          c.attr[AttrType::DEXTERITY] = 16;
          c.barehandedDamage = 4;
          c.animal = true;
          c.name = "giant ant";);
    case CreatureId::ANT_SOLDIER:
      return CATTR(
          c.viewId = ViewId::ANT_SOLDIER;
          c.attr[AttrType::SPEED] = 130;
          c.attackEffect = EffectType(EffectId::LASTING, LastingEffect::POISON);
          c.body = Body::nonHumanoid(Body::Size::MEDIUM)
              .setWeight(10)
              .setBodyParts({{BodyPart::LEG, 6}, {BodyPart::HEAD, 1}, {BodyPart::TORSO, 1}})
              .setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 24;
          c.attr[AttrType::DEXTERITY] = 24;
          c.barehandedDamage = 12;
          c.animal = true;
          c.name = "soldier ant";);
    case CreatureId::ANT_QUEEN:      
      return CATTR(
          c.viewId = ViewId::ANT_QUEEN;
          c.attr[AttrType::SPEED] = 130;
          c.attackEffect = EffectType(EffectId::LASTING, LastingEffect::POISON);
          c.body = Body::nonHumanoid(Body::Size::MEDIUM)
              .setWeight(10)
              .setBodyParts({{BodyPart::LEG, 6}, {BodyPart::HEAD, 1}, {BodyPart::TORSO, 1}})
              .setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 28;
          c.attr[AttrType::DEXTERITY] = 28;
          c.barehandedDamage = 20;
          c.animal = true;
          c.name = "ant queen";);
    case CreatureId::SNAKE: 
      return CATTR(
          c.viewId = ViewId::SNAKE;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoid(Body::Size::SMALL)
              .setWeight(2)
              .setBodyParts({{BodyPart::HEAD, 1}, {BodyPart::TORSO, 1}})
              .setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 2;
          c.attr[AttrType::DEXTERITY] = 14;
          c.barehandedDamage = 15;
          c.animal = true;
          c.attackEffect = EffectType(EffectId::LASTING, LastingEffect::POISON);
          c.skills.insert(SkillId::SWIMMING);
          c.name = "snake";);
    case CreatureId::RAVEN: 
      return CATTR(
          c.viewId = ViewId::RAVEN;
          c.attr[AttrType::SPEED] = 250;
          c.body = Body::nonHumanoid(Body::Size::SMALL).setWeight(0.5).setBirdBodyParts().setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 2;
          c.attr[AttrType::DEXTERITY] = 12;
          c.barehandedDamage = 0;
          c.animal = true;
          c.noChase = true;
          c.courage = 100;
          c.spawnType = SpawnType::BEAST;
          c.minionTasks.setValue(MinionTask::EXPLORE, 1);
          c.minionTasks.setValue(MinionTask::LAIR, 1);
          c.name = "raven";
          c.name->setGroup("flock");
          );
    case CreatureId::VULTURE: 
      return CATTR(
          c.viewId = ViewId::VULTURE;
          c.attr[AttrType::SPEED] = 80;
          c.body = Body::nonHumanoid(Body::Size::SMALL).setWeight(5).setBirdBodyParts().setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 2;
          c.attr[AttrType::DEXTERITY] = 12;
          c.barehandedDamage = 0;
          c.animal = true;
          c.noChase = true;
          c.courage = 100;
          c.spawnType = SpawnType::BEAST;
          c.minionTasks.setValue(MinionTask::EXPLORE, 1);
          c.minionTasks.setValue(MinionTask::LAIR, 1);
          c.name = "vulture";);
    case CreatureId::WOLF: 
      return CATTR(
          c.viewId = ViewId::WOLF;
          c.attr[AttrType::SPEED] = 160;
          c.body = Body::nonHumanoid(Body::Size::MEDIUM).setWeight(35).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 18;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 12;
          c.animal = true;
          c.skills.insert(SkillId::NIGHT_VISION);
          c.spawnType = SpawnType::BEAST;
          c.name = CreatureName("wolf", "wolves");
          c.name->setGroup("pack");
          c.name->setFirst(NameGenerator::get(NameGeneratorId::DOG)->getNext());
          c.minionTasks.setValue(MinionTask::EXPLORE_NOCTURNAL, 1);
          c.minionTasks.setValue(MinionTask::LAIR, 1);
          );    
    case CreatureId::WEREWOLF:
      return CATTR(
          c.viewId = ViewId::WEREWOLF;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 18;
          c.attr[AttrType::DEXTERITY] = 17;
          c.barehandedDamage = 12;
          c.animal = true;
          c.spawnType = SpawnType::BEAST;
          c.skills.insert(SkillId::NIGHT_VISION);
          c.skills.insert(SkillId::STEALTH);
          c.minionTasks.setValue(MinionTask::EXPLORE_NOCTURNAL, 1);
          c.minionTasks.setValue(MinionTask::TRAIN, 1);
          c.minionTasks.setValue(MinionTask::LAIR, 1);
          c.minionTasks.setValue(MinionTask::EAT, 3);
          c.name = CreatureName("werewolf", "werewolves");
          c.name->setFirst(NameGenerator::get(NameGeneratorId::DOG)->getNext());
          );
    case CreatureId::DOG: 
      return CATTR(
          c.viewId = ViewId::DOG;
          c.attr[AttrType::SPEED] = 160;
          c.body = Body::nonHumanoid(Body::Size::MEDIUM).setWeight(25).setHorseBodyParts();
          c.attr[AttrType::STRENGTH] = 15;
          c.attr[AttrType::DEXTERITY] = 13;
          c.barehandedDamage = 12;
          c.animal = true;
          c.innocent = true;
          c.name = "dog";
          c.name->setGroup("pack");
          c.name->setFirst(NameGenerator::get(NameGeneratorId::DOG)->getNext());
          );
    case CreatureId::FIRE_SPHERE: 
      return CATTR(
          c.viewId = ViewId::FIRE_SPHERE;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoid(Body::Material::FIRE, Body::Size::SMALL).setDeathSound(none);
          c.attr[AttrType::STRENGTH] = 5;
          c.attr[AttrType::DEXTERITY] = 15;
          c.barehandedDamage = 10;
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.name = "fire sphere";);
    case CreatureId::ELEMENTALIST: 
      return CATTR(
          c.viewId = ViewId::ELEMENTALIST;
          c.attr[AttrType::SPEED] = 120;
          c.body = Body::humanoid(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 16;
          c.attr[AttrType::DEXTERITY] = 14;
          c.barehandedDamage = 3;
          c.gender = Gender::female;
          c.permanentEffects[LastingEffect::FIRE_RESISTANT] = 1;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.name = "elementalist";
          c.name->setFirst(NameGenerator::get(NameGeneratorId::FIRST_FEMALE)->getNext());
          );
    case CreatureId::FIRE_ELEMENTAL:
      return CATTR(
          c.viewId = ViewId::FIRE_ELEMENTAL;
          c.body = Body::nonHumanoid(Body::Material::FIRE, Body::Size::LARGE).setDeathSound(none);
          c.attr[AttrType::SPEED] = 120;
          c.attr[AttrType::STRENGTH] = 25;
          c.attr[AttrType::DEXTERITY] = 30;
          c.barehandedAttack = AttackType::HIT;
          c.barehandedDamage = 10;
          c.attackEffect = EffectId::FIRE;
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.name = "fire elemental";);
    case CreatureId::AIR_ELEMENTAL:
      return CATTR(
          c.viewId = ViewId::AIR_ELEMENTAL;
          c.body = Body::nonHumanoid(Body::Material::SPIRIT, Body::Size::LARGE).setDeathSound(none);
          c.attr[AttrType::SPEED] = 160;
          c.attr[AttrType::STRENGTH] = 20;
          c.attr[AttrType::DEXTERITY] = 30;
          c.barehandedAttack = AttackType::HIT;
          c.barehandedDamage = 10;
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.spells->add(SpellId::AIR_BLAST);
          c.name = "air elemental";);
    case CreatureId::EARTH_ELEMENTAL:
      return CATTR(
          c.viewId = ViewId::EARTH_ELEMENTAL;
          c.body = Body::nonHumanoid(Body::Material::ROCK, Body::Size::LARGE).setWeight(500)
              .setHumanoidBodyParts().setDeathSound(none);
          c.attr[AttrType::SPEED] = 80;
          c.attr[AttrType::STRENGTH] = 45;
          c.attr[AttrType::DEXTERITY] = 20;
          c.barehandedAttack = AttackType::HIT;
          c.barehandedDamage = 10;
          c.name = "earth elemental";);
    case CreatureId::WATER_ELEMENTAL:
      return CATTR(
          c.viewId = ViewId::WATER_ELEMENTAL;
          c.body = Body::nonHumanoid(Body::Material::WATER, Body::Size::LARGE).setWeight(300).setHumanoidBodyParts()
              .setDeathSound(none);
          c.attr[AttrType::SPEED] = 80;
          c.attr[AttrType::STRENGTH] = 45;
          c.attr[AttrType::DEXTERITY] = 20;
          c.barehandedAttack = AttackType::HIT;
          c.barehandedDamage = 10;
          c.skills.insert(SkillId::SWIMMING);
          c.name = "water elemental";);
    case CreatureId::ENT:
      return CATTR(
          c.viewId = ViewId::ENT;
          c.body = Body::nonHumanoid(Body::Material::WOOD, Body::Size::HUGE).setHumanoidBodyParts();
          c.attr[AttrType::SPEED] = 30;
          c.attr[AttrType::STRENGTH] = 40;
          c.attr[AttrType::DEXTERITY] = 26;
          c.barehandedDamage = 0;
          c.skills.insert(SkillId::ELF_VISION);
          c.minionTasks.clear();
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.name = "tree spirit";);
    case CreatureId::ANGEL:
      return CATTR(
          c.viewId = ViewId::ANGEL;
          c.attr[AttrType::SPEED] = 100;
          c.body = Body::nonHumanoid(Body::Material::SPIRIT, Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 30;
          c.attr[AttrType::DEXTERITY] = 22;
          c.barehandedDamage = 3;
          c.chatReactionFriendly = "curses all dungeons"_s;
          c.chatReactionHostile = "\"Die!\""_s;
          c.skills.setValue(SkillId::WEAPON_MELEE, 0.3);
          c.name = "angel";);
    case CreatureId::KRAKEN:
      return getKrakenAttributes(ViewId::KRAKEN_HEAD, "kraken");
    case CreatureId::BAT: 
      return CATTR(
          c.viewId = ViewId::BAT;
          c.body = Body::nonHumanoid(Body::Size::SMALL).setWeight(1).setBirdBodyParts();
          c.attr[AttrType::SPEED] = 150;
          c.attr[AttrType::STRENGTH] = 3;
          c.attr[AttrType::DEXTERITY] = 16;
          c.barehandedDamage = 2;
          c.animal = true;
          c.noChase = true;
          c.courage = 100;
          c.spawnType = SpawnType::BEAST;
          c.skills.insert(SkillId::NIGHT_VISION);
          c.minionTasks.setValue(MinionTask::EXPLORE_CAVES, 1);
          c.minionTasks.setValue(MinionTask::EXPLORE_NOCTURNAL, 1);
          c.minionTasks.setValue(MinionTask::LAIR, 1);
          c.name = "bat";);
    case CreatureId::DEATH: 
      return CATTR(
          c.viewId = ViewId::DEATH;
          c.attr[AttrType::SPEED] = 95;
          c.body = Body::humanoidSpirit(Body::Size::LARGE);
          c.attr[AttrType::STRENGTH] = 100;
          c.attr[AttrType::DEXTERITY] = 35;
          c.barehandedDamage = 10;
          c.chatReactionFriendly = c.chatReactionHostile = "\"IN ORDER TO HAVE A CHANGE OF FORTUNE AT THE LAST MINUTE "
              "YOU HAVE TO TAKE YOUR FORTUNE TO THE LAST MINUTE.\""_s;
          c.name = "Death";);
    default: FATAL << "This is not handled here " << int(id);
  }
  FATAL << "unhandled case";
  return CreatureAttributes([](CreatureAttributes&) {});
}

ControllerFactory getController(CreatureId id, MonsterAIFactory normalFactory) {
  switch (id) {
    case CreatureId::KRAKEN:
      return ControllerFactory([=](WCreature c) {
          return makeOwner<KrakenController>(c);
          });
    case CreatureId::FIRE_SPHERE:
      return ControllerFactory([=](WCreature c) {
          return makeOwner<KamikazeController>(c, normalFactory);
          });
    default: return Monster::getFactory(normalFactory);
  }
}

PCreature CreatureFactory::get(CreatureId id, TribeId tribe, MonsterAIFactory aiFactory) {
  ControllerFactory factory = Monster::getFactory(aiFactory);
  switch (id) {
    case CreatureId::SPECIAL_BLBN:
      return getSpecial(tribe, false, true, true, false, factory);
    case CreatureId::SPECIAL_BLBW:
      return getSpecial(tribe, false, true, true, true, factory);
    case CreatureId::SPECIAL_BLGN:
      return getSpecial(tribe, false, true, false, false, factory);
    case CreatureId::SPECIAL_BLGW:
      return getSpecial(tribe, false, true, false, true, factory);
    case CreatureId::SPECIAL_BMBN:
      return getSpecial(tribe, false, false, true, false, factory);
    case CreatureId::SPECIAL_BMBW:
      return getSpecial(tribe, false, false, true, true, factory);
    case CreatureId::SPECIAL_BMGN:
      return getSpecial(tribe, false, false, false, false, factory);
    case CreatureId::SPECIAL_BMGW:
      return getSpecial(tribe, false, false, false, true, factory);
    case CreatureId::SPECIAL_HLBN:
      return getSpecial(tribe, true, true, true, false, factory);
    case CreatureId::SPECIAL_HLBW:
      return getSpecial(tribe, true, true, true, true, factory);
    case CreatureId::SPECIAL_HLGN:
      return getSpecial(tribe, true, true, false, false, factory);
    case CreatureId::SPECIAL_HLGW:
      return getSpecial(tribe, true, true, false, true, factory);
    case CreatureId::SPECIAL_HMBN:
      return getSpecial(tribe, true, false, true, false, factory);
    case CreatureId::SPECIAL_HMBW:
      return getSpecial(tribe, true, false, true, true, factory);
    case CreatureId::SPECIAL_HMGN:
      return getSpecial(tribe, true, false, false, false, factory);
    case CreatureId::SPECIAL_HMGW:
      return getSpecial(tribe, true, false, false, true, factory);
    case CreatureId::SOKOBAN_BOULDER:
      return getSokobanBoulder(tribe);
    default: return get(getAttributes(id), tribe, getController(id, aiFactory));
  }
}

PCreature CreatureFactory::getGhost(WCreature creature) {
  ViewObject viewObject(creature->getViewObject().id(), ViewLayer::CREATURE, "Ghost");
  viewObject.setModifier(ViewObject::Modifier::ILLUSION);
  auto ret = makeOwner<Creature>(viewObject, creature->getTribeId(), getAttributes(CreatureId::LOST_SOUL));
  ret->setController(Monster::getFactory(MonsterAIFactory::monster()).get(ret.get()));
  return ret;
}

ItemType randomHealing() {
  return Random.choose(ItemType(ItemId::POTION, EffectId::HEAL), ItemType(ItemId::FIRST_AID_KIT));
}

ItemType randomBackup() {
  return Random.choose({ ItemType(ItemId::SCROLL, EffectId::DECEPTION), ItemType(ItemId::SCROLL, EffectId::TELEPORT),
      randomHealing()}, {1, 1, 8});
}

ItemType randomArmor() {
  return Random.choose({ItemId::LEATHER_ARMOR, ItemId::CHAIN_ARMOR}, {4, 1});
}

class ItemList {
  public:
  ItemList& maybe(double chance, ItemType id, int num = 1) {
    if (Random.getDouble() <= chance)
      add(id, num);
    return *this;
  }

  ItemList& maybe(double chance, const vector<ItemType>& ids) {
    if (Random.getDouble() <= chance)
      for (ItemType id : ids)
        add(id);
    return *this;
  }

  ItemList& add(ItemType id, int num = 1) {
    for (int i : Range(num))
      ret.push_back(id);
    return *this;
  }

  ItemList& add(vector<ItemType> ids) {
    for (ItemType id : ids)
      ret.push_back(id);
    return *this;
  }

  operator vector<ItemType>() {
    return ret;
  }

  private:
  vector<ItemType> ret;
};

vector<ItemType> getInventory(CreatureId id) {
  switch (id) {
    case CreatureId::CYCLOPS: 
      return ItemList().add(ItemId::HEAVY_CLUB).add(ItemId::GOLD_PIECE, Random.get(200, 400));
    case CreatureId::GREEN_DRAGON:
      return ItemList().add(ItemId::GOLD_PIECE, Random.get(300, 500));
    case CreatureId::RED_DRAGON:
      return ItemList().add(ItemId::GOLD_PIECE, Random.get(600, 1000));
    case CreatureId::ANGEL:
      return ItemList().add(ItemId::SPECIAL_SWORD);
    case CreatureId::KEEPER_F:
    case CreatureId::KEEPER:
      return ItemList()
          .add(ItemId::ROBE);
    case CreatureId::ADVENTURER_F:
    case CreatureId::ADVENTURER:
      return ItemList()
        .add(ItemId::FIRST_AID_KIT)
        .add(ItemId::KNIFE)
        .add(ItemId::SWORD)
        .add(ItemId::LEATHER_GLOVES)
        .add(ItemId::LEATHER_ARMOR)
        .add(ItemId::LEATHER_HELM)
        .add(ItemId::GOLD_PIECE, Random.get(80, 130));
    case CreatureId::DEATH:
      return ItemList()
        .add(ItemId::SCYTHE);
    case CreatureId::LEPRECHAUN: 
      return ItemList()
        .add({ItemId::SCROLL, EffectId::TELEPORT}, Random.get(1, 4));
    case CreatureId::GOBLIN: 
      return ItemList()
        .add(ItemId::CLUB)
        .maybe(0.3, ItemId::LEATHER_BOOTS);
    case CreatureId::WARRIOR: 
      return ItemList()
        .add(ItemId::LEATHER_ARMOR)
        .add(ItemId::CLUB)
        .add(ItemId::GOLD_PIECE, Random.get(10, 20));
    case CreatureId::SHAMAN: 
      return ItemList()
        .add(ItemId::LEATHER_ARMOR)
        .add(ItemId::CLUB);
    case CreatureId::LIZARDLORD: 
    case CreatureId::LIZARDMAN: 
      return ItemList().add(ItemId::LEATHER_ARMOR)
        .add(ItemId::GOLD_PIECE, Random.get(10, 20));
    case CreatureId::HARPY: 
      return ItemList()
        .add(ItemId::BOW).add(ItemId::ARROW, Random.get(20, 36));
    case CreatureId::ARCHER: 
      return ItemList()
        .add(ItemId::BOW).add(ItemId::ARROW, Random.get(20, 36))
        .add(ItemId::KNIFE)
        .add(ItemId::LEATHER_ARMOR)
        .add(ItemId::LEATHER_BOOTS)
        .add(randomHealing())
        .add(ItemId::GOLD_PIECE, Random.get(20, 50));
    case CreatureId::CASTLE_GUARD:
    case CreatureId::WITCHMAN:
      return ItemList()
        .add(ItemId::SWORD)
        .add(ItemId::LEATHER_ARMOR)
        .add(ItemId::LEATHER_BOOTS)
        .add(randomHealing())
        .add({ItemId::POTION, EffectType{EffectId::LASTING, LastingEffect::SPEED}}, 4)
        .add(ItemId::GOLD_PIECE, Random.get(350, 500));
    case CreatureId::KNIGHT: 
      return ItemList()
        .add(ItemId::SWORD)
        .add(ItemId::CHAIN_ARMOR)
        .add(ItemId::LEATHER_BOOTS)
        .add(randomHealing())
        .add(ItemId::GOLD_PIECE, Random.get(30, 80));
    case CreatureId::MINOTAUR: 
      return ItemList()
        .add(ItemId::BATTLE_AXE);
    case CreatureId::AVATAR: 
      return ItemList()
        .add(ItemId::SPECIAL_BATTLE_AXE)
        .add(ItemId::CHAIN_ARMOR)
        .add(ItemId::IRON_HELM)
        .add(ItemId::IRON_BOOTS)
        .add({ItemId::POTION, EffectId::HEAL}, Random.get(1, 4))
        .add(ItemId::GOLD_PIECE, Random.get(200, 300));
    case CreatureId::OGRE: 
      return ItemList().add(ItemId::HEAVY_CLUB);
    case CreatureId::BANDIT:
    case CreatureId::ORC: 
      return ItemList()
        .add(ItemId::SWORD)
        .maybe(0.3, randomBackup())
        .maybe(0.05, ItemList().add(ItemId::BOW).add(ItemId::ARROW, Random.get(20, 36)));
    case CreatureId::DWARF: 
      return ItemList()
        .add(Random.choose({ItemId::BATTLE_AXE, ItemId::WAR_HAMMER}, {1, 1}))
        .maybe(0.6, randomBackup())
        .add(ItemId::CHAIN_ARMOR)
        .maybe(0.5, ItemId::IRON_HELM)
        .maybe(0.3, ItemId::IRON_BOOTS)
        .add(ItemId::GOLD_PIECE, Random.get(10, 30));
    case CreatureId::DWARF_BARON: 
      return ItemList()
        .add(Random.choose({ItemId::SPECIAL_BATTLE_AXE, ItemId::SPECIAL_WAR_HAMMER}, {1, 1}))
        .add(randomBackup())
        .add(ItemId::CHAIN_ARMOR)
        .add(ItemId::IRON_BOOTS)
        .add(ItemId::IRON_HELM)
        .add(ItemId::GOLD_PIECE, Random.get(200, 400));
    case CreatureId::GNOME_CHIEF:
      return ItemList()
        .add(ItemId::SWORD)
        .add(randomBackup());
    case CreatureId::DARK_ELF_LORD: 
    case CreatureId::ELF_LORD: 
      return ItemList()
        .add(ItemId::SPECIAL_ELVEN_SWORD)
        .add(ItemId::LEATHER_ARMOR)
        .add(ItemId::BOW)
        .add(ItemId::ARROW, Random.get(20, 36))
        .add(ItemId::GOLD_PIECE, Random.get(100, 300))
        .add(randomBackup());
    case CreatureId::DRIAD: 
      return ItemList()
        .add(ItemId::BOW)
        .add(ItemId::ARROW, Random.get(20, 36));
    case CreatureId::DARK_ELF_WARRIOR: 
      return ItemList()
        .add(ItemId::ELVEN_SWORD)
        .add(ItemId::LEATHER_ARMOR)
        .add(ItemId::GOLD_PIECE, Random.get(10, 30))
        .add(randomBackup());
    case CreatureId::ELF_ARCHER: 
      return ItemList()
        .add(ItemId::ELVEN_SWORD)
        .add(ItemId::LEATHER_ARMOR)
        .add(ItemId::BOW)
        .add(ItemId::ARROW, Random.get(20, 36))
        .add(ItemId::GOLD_PIECE, Random.get(10, 30))
        .add(randomBackup());
    case CreatureId::WITCH: 
      return ItemList()
        .add(ItemId::KNIFE)
        .add({
            {ItemId::POTION, EffectType(EffectId::HEAL)},
            {ItemId::POTION, EffectType(EffectId::LASTING, LastingEffect::SLEEP)},
            {ItemId::POTION, EffectType(EffectId::LASTING, LastingEffect::SLOWED)},
            {ItemId::POTION, EffectType(EffectId::LASTING, LastingEffect::BLIND)},
            {ItemId::POTION, EffectType(EffectId::LASTING, LastingEffect::INVISIBLE)},
            {ItemId::POTION, EffectType(EffectId::LASTING, LastingEffect::POISON)},
            {ItemId::POTION, EffectType(EffectId::LASTING, LastingEffect::SPEED)}});
    default: return {};
  }
}

PCreature CreatureFactory::fromId(CreatureId id, TribeId t) {
  return fromId(id, t, MonsterAIFactory::monster());
}

PCreature CreatureFactory::fromId(CreatureId id, TribeId t, const MonsterAIFactory& factory) {
  return addInventory(get(id, t, factory), getInventory(id));
}

