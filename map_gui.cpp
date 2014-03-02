#include "stdafx.h"

#include "map_gui.h"
#include "view_object.h"
#include "map_layout.h"
#include "view_index.h"
#include "tile.h"
#include "window_view.h"

using namespace colors;

MapGui::MapGui(Rectangle bounds, const Table<Optional<ViewIndex>>& o)
  : GuiElem(bounds), objects(o) {
}

void MapGui::setLayout(MapLayout* l) {
  layout = l;
}

void MapGui::setSpriteMode(bool s) {
  spriteMode = s;
}

Optional<Vec2> MapGui::getHighlightedTile(Renderer& renderer) {
  Vec2 pos = renderer.getMousePos();
  if (!pos.inRectangle(getBounds()))
    return Nothing();
  return layout->projectOnMap(getBounds(), pos);
}

static Color getBleedingColor(const ViewObject& object) {
  double bleeding = object.getBleeding();
 /* if (object.isPoisoned())
    return Color(0, 255, 0);*/
  if (bleeding > 0)
    bleeding = 0.3 + bleeding * 0.7;
  return Color(255, max(0., (1 - bleeding) * 255), max(0., (1 - bleeding) * 255));
}

Color getHighlightColor(ViewIndex::HighlightInfo info) {
  switch (info.type) {
    case HighlightType::BUILD: return transparency(yellow, 170);
    case HighlightType::RECT_SELECTION: return transparency(yellow, 90);
    case HighlightType::FOG: return transparency(white, 120 * info.amount);
    case HighlightType::POISON_GAS: return Color(0, min(255., info.amount * 500), 0, info.amount * 140);
    case HighlightType::MEMORY: return transparency(black, 80);
  }
  FAIL << "pokpok";
  return black;
}

void MapGui::drawHint(Renderer& renderer, Color color, const string& text) {
  int height = 30;
  int width = renderer.getTextLength(text) + 30;
  Vec2 pos(getBounds().getKX() - width, getBounds().getKY() - height);
  renderer.drawFilledRectangle(pos.x, pos.y, pos.x + width, pos.y + height, transparency(black, 190));
  renderer.drawText(color, pos.x + 10, pos.y + 1, text);
}

vector<Vec2> getConnectionDirs(ViewId id) {
  return Vec2::directions4();
}

enum class ConnectionId {
  ROAD,
  WALL,
  WATER,
  MOUNTAIN2,
};

map<Vec2, ConnectionId> floorIds;
set<Vec2> shadowed;


Optional<ConnectionId> getConnectionId(ViewId id) {
  switch (id) {
    case ViewId::ROAD: return ConnectionId::ROAD;
    case ViewId::BLACK_WALL:
    case ViewId::YELLOW_WALL:
    case ViewId::HELL_WALL:
    case ViewId::LOW_ROCK_WALL:
    case ViewId::WOOD_WALL:
    case ViewId::CASTLE_WALL:
    case ViewId::MUD_WALL:
    case ViewId::WALL: return ConnectionId::WALL;
    case ViewId::MAGMA:
    case ViewId::WATER: return ConnectionId::WATER;
    case ViewId::MOUNTAIN2: return ConnectionId::MOUNTAIN2;
    default: return Nothing();
  }
}

bool tileConnects(ViewId id, Vec2 pos) {
  return floorIds.count(pos) && getConnectionId(id) == floorIds.at(pos);
}

void MapGui::onLeftClick(Vec2) {
}
void MapGui::onRightClick(Vec2) {
}
void MapGui::onMouseMove(Vec2) {
}


Optional<ViewObject> MapGui::drawObjectAbs(Renderer& renderer, int x, int y, const ViewIndex& index,
    int sizeX, int sizeY, Vec2 tilePos, bool highlighted) {
  vector<ViewObject> objects;
  if (spriteMode) {
    for (ViewLayer layer : layout->getLayers())
      if (index.hasObject(layer))
        objects.push_back(index.getObject(layer));
  } else
    if (auto object = index.getTopObject(layout->getLayers()))
      objects.push_back(*object);
  for (ViewObject& object : objects) {
    if (object.isPlayer()) {
      renderer.drawFilledRectangle(x, y, x + sizeX, y + sizeY, Color::Transparent, lightGray);
    }
    Tile tile = Tile::getTile(object, spriteMode);
    Color color = getBleedingColor(object);
    if (object.isInvisible())
      color = transparency(color, 70);
    else
    if (tile.translucent > 0)
      color = transparency(color, 255 * (1 - tile.translucent));
    else if (object.isIllusion())
      color = transparency(color, 150);

    if (object.getWaterDepth() > 0) {
      int val = max(0.0, 255.0 - min(2.0, object.getWaterDepth()) * 60);
      color = Color(val, val, val);
    }
    if (tile.hasSpriteCoord()) {
      int moveY = 0;
      int off = (Renderer::nominalSize -  Renderer::tileSize[tile.getTexNum()]) / 2;
      int sz = Renderer::tileSize[tile.getTexNum()];
      int width = sizeX - 2 * off;
      int height = sizeY - 2 * off;
      set<Dir> dirs;
      for (Vec2 dir : getConnectionDirs(object.id()))
        if (tileConnects(object.id(), tilePos + dir))
          dirs.insert(dir.getCardinalDir());
      Vec2 coord = tile.getSpriteCoord(dirs);

      if (object.layer() == ViewLayer::CREATURE) {
        renderer.drawSprite(x, y, 2 * Renderer::nominalSize, 22 * Renderer::nominalSize, Renderer::nominalSize, Renderer::nominalSize, Renderer::tiles[0], width, height);
        moveY = -4 - object.getSizeIncrease() / 2;
      }
      renderer.drawSprite(x + off, y + moveY + off, coord.x * sz,
          coord.y * sz, sz, sz, Renderer::tiles[tile.getTexNum()], width, height, color);
      if (contains({ViewLayer::FLOOR, ViewLayer::FLOOR_BACKGROUND}, object.layer()) && 
          shadowed.count(tilePos) && !tile.stickingOut)
        renderer.drawSprite(x, y, 1 * Renderer::nominalSize, 21 * Renderer::nominalSize, Renderer::nominalSize, Renderer::nominalSize, Renderer::tiles[5], width, height);
      if (object.getBurning() > 0) {
        renderer.drawSprite(x, y, Random.getRandom(10, 12) * Renderer::nominalSize, 0 * Renderer::nominalSize,
            Renderer::nominalSize, Renderer::nominalSize, Renderer::tiles[2], width, height);
      }
    } else {
      renderer.drawText(tile.symFont ? Renderer::SYMBOL_FONT : Renderer::TILE_FONT,
          sizeY + object.getSizeIncrease(), Tile::getColor(object),
          x + sizeX / 2, y - 3 - object.getSizeIncrease(), tile.text, true);
      if (object.getBurning() > 0) {
        renderer.drawText(Renderer::SYMBOL_FONT, sizeY, WindowView::getFireColor(),
            x + sizeX / 2, y - 3, L'ѡ', true);
        if (object.getBurning() > 0.5)
          renderer.drawText(Renderer::SYMBOL_FONT, sizeY, WindowView::getFireColor(),
              x + sizeX / 2, y - 3, L'Ѡ', true);
      }
    }
  }
  if (highlighted) {
    renderer.drawFilledRectangle(x, y, x + sizeX, y + sizeY, Color::Transparent, lightGray);
  }
  if (auto highlight = index.getHighlight())
    renderer.drawFilledRectangle(x, y, x + sizeX, y + sizeY, getHighlightColor(*highlight));
  if (!objects.empty())
    return objects.back();
  else
    return Nothing();
}

void MapGui::setLevelBounds(Rectangle b) {
  levelBounds = b;
}

void MapGui::updateObjects(const MapMemory* mem) {
  lastMemory = mem;
  floorIds.clear();
  shadowed.clear();
  for (Vec2 wpos : layout->getAllTiles(getBounds(), objects.getBounds()))
    if (auto index = objects[wpos])
      if (index->hasObject(ViewLayer::FLOOR)) {
        ViewObject object = index->getObject(ViewLayer::FLOOR);
        if (object.castsShadow()) {
          shadowed.erase(wpos);
          shadowed.insert(wpos + Vec2(0, 1));
        }
        if (auto id = getConnectionId(object.id()))
          floorIds.insert(make_pair(wpos, *id));
      }
}


void MapGui::render(Renderer& renderer) {
  int sizeX = layout->squareWidth();
  int sizeY = layout->squareHeight();
  renderer.drawFilledRectangle(getBounds(), almostBlack);
  Optional<ViewObject> highlighted;
  Optional<Vec2> highlightedPos = getHighlightedTile(renderer);
  for (Vec2 wpos : layout->getAllTiles(getBounds(), objects.getBounds())) {
    Vec2 pos = layout->projectOnScreen(getBounds(), wpos);
    if (!spriteMode && wpos.inRectangle(levelBounds))
      renderer.drawFilledRectangle(pos.x, pos.y, pos.x + sizeX, pos.y + sizeY, black);
    if (!objects[wpos] || objects[wpos]->isEmpty()) {
      if (wpos.inRectangle(levelBounds))
        renderer.drawFilledRectangle(pos.x, pos.y, pos.x + sizeX, pos.y + sizeY, black);
      if (highlightedPos == wpos) {
        renderer.drawFilledRectangle(pos.x, pos.y, pos.x + sizeX, pos.y + sizeY, Color::Transparent, lightGray);
      }
      continue;
    }
    const ViewIndex& index = *objects[wpos];
    bool isHighlighted = highlightedPos == wpos;
    if (auto topObject = drawObjectAbs(renderer, pos.x, pos.y, index, sizeX, sizeY, wpos, isHighlighted)) {
      if (isHighlighted)
        highlighted = *topObject;
    }
  }
  if (highlightedPos && highlighted) {
    Color col = white;
    if (highlighted->isHostile())
      col = red;
    else if (highlighted->isFriendly())
      col = green;
    drawHint(renderer, col, highlighted->getDescription(true));
  }
}

