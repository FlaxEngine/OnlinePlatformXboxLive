// Copyright (c) 2012-2022 Wojciech Figat. All rights reserved.

#pragma once

#if PLATFORM_GDK

#include "Engine/Online/IOnlinePlatform.h"
#include "Engine/Scripting/ScriptingObject.h"
#include "Engine/Core/Collections/Dictionary.h"

/// <summary>
/// The online platform implementation for Xbox Live.
/// </summary>
API_CLASS(Sealed, Namespace="FlaxEngine.Online.XboxLive") class ONLINEPLATFORMXBOXLIVE_API OnlinePlatformXboxLive : public ScriptingObject, public IOnlinePlatform
{
    DECLARE_SCRIPTING_TYPE(OnlinePlatformXboxLive);
private:
    struct XTaskQueueObject* _taskQueue = nullptr;
    uint32 _titleId;
    Dictionary<User*, struct XblContext*> _users;
    Dictionary<User*, struct XGameSaveProvider*> _gameSaveProviders;

public:
    // [IOnlinePlatform]
    bool Initialize() override;
    void Deinitialize() override;
    bool UserLogin(User* localUser) override;
    bool UserLogout(User* localUser) override;
    bool GetUserLoggedIn(User* localUser) override;
    bool GetUser(OnlineUser& user, User* localUser) override;
    bool GetFriends(Array<OnlineUser, HeapAllocation>& friends, User* localUser) override;
    bool GetAchievements(Array<OnlineAchievement, HeapAllocation>& achievements, User* localUser) override;
    bool UnlockAchievement(const StringView& name, User* localUser) override;
    bool UnlockAchievementProgress(const StringView& name, float progress, User* localUser) override;
#if !BUILD_RELEASE
    bool ResetAchievements(User* localUser) override;
#endif
    bool GetStat(const StringView& name, float& value, User* localUser) override;
    bool SetStat(const StringView& name, float value, User* localUser) override;
    bool GetLeaderboard(const StringView& name, OnlineLeaderboard& value, User* localUser) override;
    bool GetOrCreateLeaderboard(const StringView& name, OnlineLeaderboardSortModes sortMode, OnlineLeaderboardValueFormats valueFormat, OnlineLeaderboard& value, User* localUser) override;
    bool GetLeaderboardEntries(const OnlineLeaderboard& leaderboard, Array<OnlineLeaderboardEntry, HeapAllocation>& entries, int32 start, int32 count) override;
    bool GetLeaderboardEntriesAroundUser(const OnlineLeaderboard& leaderboard, Array<OnlineLeaderboardEntry, HeapAllocation>& entries, int32 start, int32 count) override;
    bool GetLeaderboardEntriesForFriends(const OnlineLeaderboard& leaderboard, Array<OnlineLeaderboardEntry, HeapAllocation>& entries) override;
    bool GetLeaderboardEntriesForUsers(const OnlineLeaderboard& leaderboard, Array<OnlineLeaderboardEntry, HeapAllocation>& entries, const Array<OnlineUser, HeapAllocation>& users) override;
    bool SetLeaderboardEntry(const OnlineLeaderboard& leaderboard, int32 score, bool keepBest) override;
    bool GetSaveGame(const StringView& name, Array<byte, HeapAllocation>& data, User* localUser) override;
    bool SetSaveGame(const StringView& name, const Span<byte>& data, User* localUser) override;

private:
    bool GetSaveGameProvider(User*& localUser, XGameSaveProvider*& provider);
    bool GetLeaderboardContext(const OnlineLeaderboard& leaderboard, struct XblLeaderboardsContext& context) const;
    bool GetLeaderboardEntries(XblLeaderboardsContext& context) const;
    bool GetContext(User*& localUser, XblContext*& context) const;
    void OnUpdate();
};

#endif
