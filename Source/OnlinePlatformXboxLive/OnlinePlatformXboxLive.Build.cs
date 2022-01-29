// Copyright (c) 2012-2022 Wojciech Figat. All rights reserved.

using Flax.Build;
using Flax.Build.NativeCpp;

/// <summary>
/// Online services module for Xbox Live platform.
/// </summary>
public class OnlinePlatformXboxLive : GameModule
{
    /// <inheritdoc />
    public override void Setup(BuildOptions options)
    {
        base.Setup(options);

        options.PublicDependencies.Add("Online");

        if (options.Platform is Flax.Build.Platforms.GDKPlatform)
        {
            options.Libraries.Add("libHttpClient.142.GDK.C.lib");
            options.Libraries.Add("Crypt32.lib");
        }
    }
}
