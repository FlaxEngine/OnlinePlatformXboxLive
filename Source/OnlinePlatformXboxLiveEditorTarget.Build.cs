using Flax.Build;

public class OnlinePlatformXboxLiveEditorTarget : GameProjectEditorTarget
{
    /// <inheritdoc />
    public override void Init()
    {
        base.Init();

        Modules.Add("OnlinePlatformXboxLive");
    }
}
