#include <Graphics/DrawBus.hpp>

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Teller::Internal
{
  namespace
  {
    struct Bus
    {
      std::vector<DrawListener*> listeners;
      std::vector<DrawMessage> pending;
      std::vector<DrawMessage> committed;
      std::unordered_set<const void*> subscribedSources;
    };

    // グローバルのAssetとWindowのデストラクタから呼ばれるので、破棄順に左右されないよう解放しない
    Bus& bus()
    {
      static Bus* instance = new Bus;
      return *instance;
    }

#ifndef NDEBUG
    void warnIfNeverSubscribed(const Bus& state)
    {
      static bool warned = false;
      if (warned)
      {
        return;
      }
      for (const DrawMessage& message : state.committed)
      {
        if (!state.subscribedSources.contains(message.image->source))
        {
          warned = true;
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Image: show() has no effect because no Window has subscribed to its Asset");
          return;
        }
      }
    }
#endif
  }

  ImageData::ImageData(const void* source, SDL_Surface* surface)
    : source(source)
    , surface(surface)
  {
  }

  ImageData::~ImageData()
  {
    for (DrawListener* listener : bus().listeners)
    {
      listener->onImageReleased(this);
    }
    SDL_DestroySurface(surface);
  }

  void addDrawListener(DrawListener& listener)
  {
    bus().listeners.push_back(&listener);
  }

  void removeDrawListener(DrawListener& listener)
  {
    std::erase(bus().listeners, &listener);
  }

  void publishDraw(DrawMessage message)
  {
    bus().pending.push_back(std::move(message));
  }

  void commitDraw(const void* source)
  {
    Bus& state = bus();
    const auto isFromSource = [source](const DrawMessage& message)
    {
      return message.image->source == source;
    };
    std::ranges::copy_if(state.pending, std::back_inserter(state.committed), isFromSource);
    std::erase_if(state.pending, isFromSource);
  }

  void deliverDraw()
  {
    Bus& state = bus();

#ifndef NDEBUG
    warnIfNeverSubscribed(state);
#endif

    for (DrawListener* listener : state.listeners)
    {
      listener->onFrame(state.committed);
    }
    state.committed.clear();
    state.pending.clear();
  }

  void noteSubscribed(const void* source)
  {
    bus().subscribedSources.insert(source);
  }

  void notifySourceReleased(const void* source)
  {
    Bus& state = bus();
    state.subscribedSources.erase(source);
    for (DrawListener* listener : state.listeners)
    {
      listener->onSourceReleased(source);
    }
  }
}
