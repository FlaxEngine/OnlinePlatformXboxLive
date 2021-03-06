using Flax.Build;

public class OnlinePlatformXboxLiveTarget : GameProjectTarget
{
    /// <inheritdoc />
    public override void Init()
    {
        base.Init();

        Platforms = new[]
        {
            TargetPlatform.XboxOne,
            TargetPlatform.XboxScarlett,
        };

        Modules.Add("OnlinePlatformXboxLive");
    }
}
