# Xbox Live for Flax Engine

This repository contains a plugin project for [Flax Engine](https://flaxengine.com/) games with [Xbox Live](https://docs.microsoft.com/en-us/gaming/xbox-live/) online platform implementation that covers: user profile, friends list, online presence, achevements, cloud savegames and more.

Minimum supported Flax version: `1.3`.

## Installation

1. Clone repo into `<game-project>\Plugins\OnlinePlatformXboxLive`

2. Add reference to *OnlinePlatformXboxLive* project in your game by modyfying `<game-project>.flaxproj` as follows:

```
...
"References": [
    {
        "Name": "$(EnginePath)/Flax.flaxproj"
    },
    {
        "Name": "$(ProjectPath)/Plugins/OnlinePlatformXboxLive/OnlinePlatformXboxLive.flaxproj"
    }
]
```

3. Add reference to Xbox Live plugin module in you game code module by modyfying `Source/Game/Game.Build.cs` as follows (or any other game modules using Online):

```cs
/// <inheritdoc />
public override void Setup(BuildOptions options)
{
    base.Setup(options);

    ...

    switch (options.Platform.Target)
    {
    case TargetPlatform.XboxOne:
    case TargetPlatform.XboxScarlett:
        options.PublicDependencies.Add("OnlinePlatformXboxLive");
        break;
    }
}
```

This will add reference to `OnlinePlatformXboxLive` module on Xbox platforms that are supported by GDK.

4. Test it out!

Finally you can use Xbox Live as online platform in your game:

```cs
// C#
using FlaxEngine.Online;
using FlaxEngine.Online.XboxLive;

var platform = new OnlinePlatformXboxLive();
Online.Initialize(platform);
```

```cpp
// C++
#include "Engine/Online/Online.h"
#include "OnlinePlatformXboxLive/OnlinePlatformXboxLive.h"

auto platform = New<OnlinePlatformXboxLive>();
Online::Initialize(platform);
```

Then use [Online](https://docs.flaxengine.com/manual/networking/online/index.html) system to access online platform (user profile, friends, achievements, cloud saves, etc.). 

5. Setup settings

Xbox Live plugin automatically uses Xbox One or Xbox Scarlett platform settings (`GDKPlatformSettings`) so ensure to setup `TitleId`, `StoreId` and other properties used by Xbox Live.

## License

This plugin ais released under **MIT License**.
