// Copyright (c) 2012-2022 Wojciech Figat. All rights reserved.

using Flax.Build;
using Flax.Build.NativeCpp;
using Flax.Build.Platforms;

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

        if (options.Toolchain is GDKToolchain gdkToolchain)
        {
            var xboxServicesToolset = gdkToolchain.XboxServicesToolset;
            options.Libraries.Add($"libHttpClient.{(int)xboxServicesToolset}.GDK.C.lib");
            options.Libraries.Add("XCurl.lib");
            options.Libraries.Add("Crypt32.lib");
        }
    }
}
