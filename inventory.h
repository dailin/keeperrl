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

#pragma once

#include <functional>

#include "util.h"
#include "unique_entity.h"
#include "indexed_vector.h"
#include "item_index.h"

class Item;


class Inventory {
  public:
  void addItem(PItem);
  void addItems(vector<PItem>);
  static function<bool(const WItem)> getIndexPredicate(ItemIndex);
  PItem removeItem(WItem item);
  vector<PItem> removeItems(vector<WItem> items);
  vector<PItem> removeAllItems();
  void clearIndex(ItemIndex);

  const vector<WItem>& getItems() const;
  vector<WItem> getItems(function<bool (WItem)> predicate) const;
  const vector<WItem>& getItems(ItemIndex) const;

  bool hasItem(const WItem) const;
  WItem getItemById(UniqueEntity<Item>::Id) const;
  int size() const;
  double getTotalWeight() const;

  bool isEmpty() const;

  Inventory(Inventory&&) = default;
  ~Inventory();

  SERIALIZATION_DECL(Inventory);

  private:

  typedef IndexedVector<WItem, UniqueEntity<Item>::Id> ItemVector;
  typedef IndexedVector<PItem, UniqueEntity<Item>::Id> PItemVector;

  PItemVector SERIAL(items);
  ItemVector SERIAL(itemsCache);
  double SERIAL(weight) = 0;
  mutable EnumMap<ItemIndex, optional<ItemVector>> indexes;
};
