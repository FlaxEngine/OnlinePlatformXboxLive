// Copyright (c) 2012-2022 Wojciech Figat. All rights reserved.

#if FLAX_EDITOR

using System;
using FlaxEditor;

namespace FlaxEngine.Online.XboxLive
{
    /// <summary>
    /// Xbox Live online platform plugin for Editor.
    /// </summary>
    public sealed class XboxLiveEditorPlugin : EditorPlugin
    {
        /// <inheritdoc />
        public override PluginDescription Description => new PluginDescription
        {
            Name = "Xbox Live",
            Category = "Online",
            Description = "Online platform implementation for Xbox Live.",
            Author = "Flax",
            RepositoryUrl = "https://github.com/FlaxEngine/OnlinePlatformXboxLive",
            Version = new Version(1, 0),
        };
    }
}

#endif