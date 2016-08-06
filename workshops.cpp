#include "stdafx.h"
#include "workshops.h"
#include "item_factory.h"
#include "view_object.h"
#include "item.h"
#include "collective.h"

Workshops::Workshops(const EnumMap<WorkshopType, vector<Item>>& o)
    : types(o.mapValues<Type>([this] (const vector<Item>& v) { return Type(this, v);})) {
}

Workshops::Type& Workshops::get(WorkshopType type) {
  return types[type];
}

const Workshops::Type& Workshops::get(WorkshopType type) const {
  return types[type];
}

SERIALIZATION_CONSTRUCTOR_IMPL(Workshops);
SERIALIZE_DEF(Workshops, types, debt);

Workshops::Type::Type(Workshops* w, const vector<Item>& o) : options(o), workshops(w) {}

SERIALIZATION_CONSTRUCTOR_IMPL2(Workshops::Type, Type);
SERIALIZE_DEF(Workshops::Type, options, queued, workshops);


const vector<Workshops::Item>& Workshops::Type::getOptions() const {
  return options;
}

bool Workshops::Item::operator == (const Item& item) const {
  return type == item.type;
}

void Workshops::Type::stackQueue() {
  vector<Item> tmp;
  for (auto& elem : queued)
    if (!tmp.empty() && elem == tmp.back())
      tmp.back().number += elem.number;
    else
      tmp.push_back(elem);
  queued = tmp;
}

void Workshops::Type::addCost(CostInfo cost) {
  workshops->debt[cost.id] += cost.value;
}

void Workshops::Type::queue(int index) {
  const Item& newElem = options[index];
  addCost(newElem.cost);
  if (!queued.empty() && queued.back() == newElem)
    queued.back().number += newElem.number;
  else
    queued.push_back(newElem);
  stackQueue();
}

void Workshops::Type::unqueue(int index) {
  if (index >= 0 && index < queued.size()) {
    addCost(-queued[index].cost);
    queued.erase(queued.begin() + index);
  }
  stackQueue();
}

void Workshops::Type::changeNumber(int index, int number) {
  if (number <= 0)
    unqueue(index);
  else {
    if (index >= 0 && index < queued.size()) {
      auto& elem = queued[index];
      addCost(CostInfo(elem.cost.id, number - elem.number));
      elem.number = number;
    }
  }
}

static const double prodMult = 0.1;

bool Workshops::Type::isIdle() const {
  return queued.empty() || !queued[0].state;
}

void Workshops::scheduleItems(Collective* collective) {
  for (auto type : ENUM_ALL(WorkshopType))
    types[type].scheduleItems(collective);
}

void Workshops::Type::scheduleItems(Collective* collective) {
  if (queued.empty() || queued[0].state)
    return;
  for (int i : All(queued))
    if (collective->hasResource(queued[i].cost)) {
      if (i > 0)
        swap(queued[0], queued[i]);
      collective->takeResource(queued[0].cost);
      addCost(-queued[0].cost);
      queued[0].state = 0;
      return;
    }
}

vector<PItem> Workshops::Type::addWork(double amount) {
  if (!queued.empty() && queued[0].state) {
    auto& product = queued[0];
    *product.state += amount * prodMult / product.workNeeded;
    if (*product.state >= 1) {
      vector<PItem> ret = ItemFactory::fromId(product.type, product.batchSize);
      product.state = none;
      if (!--product.number)
        queued.erase(queued.begin());
      return ret;
    }
  }
  return {};
}

const vector<Workshops::Item>& Workshops::Type::getQueued() const {
  return queued;
}

Workshops::Item Workshops::Item::fromType(ItemType type, CostInfo cost, double workNeeded, int batchSize) {
  PItem item = ItemFactory::fromId(type);
  return {
    type,
    item->getPluralName(batchSize),
    item->getViewObject().id(),
    cost,
    true,
    1,
    batchSize,
    workNeeded,
    none
  };
}

int Workshops::getDebt(CollectiveResourceId resource) const {
  return debt[resource];
}

