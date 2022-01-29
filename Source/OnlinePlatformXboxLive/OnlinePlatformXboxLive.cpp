// Copyright (c) 2012-2022 Wojciech Figat. All rights reserved.

#if PLATFORM_GDK

#include "OnlinePlatformXboxLive.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Types/TimeSpan.h"
#include "Engine/Core/Collections/Array.h"
#include "Engine/Engine/Engine.h"
#include "Engine/Platform/User.h"
#include "Engine/Core/Config/PlatformSettings.h"
#include "Engine/Platform/Win32/IncludeWindowsHeaders.h"
#include <XGameRuntime.h>
#include <xsapi-c/services_c.h>

#define XBOX_LIVE_LOG(method) \
        if (FAILED(result)) \
            LOG(Error, "Xbox Live method {0} failed with result 0x{1:x}", TEXT(method), (uint32)result)
#define XBOX_LIVE_CHECK(method) \
        if (FAILED(result)) \
        { \
            LOG(Error, "Xbox Live method {0} failed with result 0x{1:x}", TEXT(method), (uint32)result); \
            return; \
        }
#define XBOX_LIVE_CHECK_RETURN(method) \
        if (FAILED(result)) \
        { \
            LOG(Error, "Xbox Live method {0} failed with result 0x{1:x}", TEXT(method), (uint32)result); \
            return true; \
        }

#define XBOX_LIVE_SAVE_GAME_BLOB_NAME "data"

void* XblMemAlloc(size_t size, HCMemoryType memoryType)
{
    return Allocator::Allocate(size);
}

void XblMemFree(void* pointer, HCMemoryType memoryType)
{
    Allocator::Free(pointer);
}

Guid GetUserId(uint64_t xboxUserId)
{
    // Xbox Live uses 64 bits, Guid is 128 bits
    const uint64 data[2] = { xboxUserId, 0 };
    return *(Guid*)data;
}

uint64_t GetXboxUserId(const Guid& id)
{
    // Xbox Live uses 64 bits, Guid is 128 bits
    return *(uint64*)&id;
}

struct XblSyncContext
{
    bool Active = true;
    bool Failed = true;
};

struct XblAchievementsContext : XblSyncContext
{
    Array<OnlineAchievement>* Achievements;
    int32 Iteration = 0;
};

struct XblStatsContext : XblSyncContext
{
    float Value;
};

struct XblPresenceContext : XblSyncContext
{
    OnlinePresenceStates Presence;
};

struct XblFriendsContext : XblSyncContext
{
    XblContextHandle Context;
    Array<uint64_t> FriendsIds;
    Array<OnlineUser>* Friends;
    int32 Iteration = 0;
};

// Waits for the async Xbox Live task to be processed in a sync manner
bool XblSyncWait(const XblSyncContext& context, XTaskQueueObject* taskQueue)
{
    // Throttle this thread to wait for the context to be deactivated
    while (XTaskQueueDispatch(taskQueue, XTaskQueuePort::Completion, 0) && context.Active)
        Platform::Sleep(1);
    return context.Failed;
}

// Waits for the async Xbox Live task to be processed in a sync manner
bool XblSyncWait(XAsyncBlock& ab, XTaskQueueObject* taskQueue)
{
    // Throttle this thread to wait for the async task to be executed
    HRESULT result;
    while ((result = XAsyncGetStatus(&ab, false)) == E_PENDING)
    {
        XTaskQueueDispatch(taskQueue, XTaskQueuePort::Completion, 0);
        Platform::Sleep(1);
    }
    return FAILED(result);
}

void XblGetAchievement(const XblAchievement& achievement, OnlineAchievement& result)
{
    result.Identifier = achievement.id;
    result.Name = result.Identifier;
    result.Title = achievement.name;
    result.IsHidden = achievement.isSecret;
    result.Progress = achievement.progressState == XblAchievementProgressState::Achieved ? 100.0f : 0.0f;
    result.Description = achievement.progressState == XblAchievementProgressState::Achieved ? achievement.unlockedDescription : achievement.lockedDescription;
    result.UnlockTime = DateTime(1601, 1, 1) + TimeSpan(achievement.progression.timeUnlocked);
}

void CALLBACK OnGetAchievements(_In_ XAsyncBlock* ab)
{
    XblAchievementsContext* achievementsContext = (XblAchievementsContext*)ab->context;
    XblAchievementsResultHandle achievementsResultHandle = nullptr;
    HRESULT result;
    if (achievementsContext->Iteration == 0)
    {
        // Get results
        result = XblAchievementsGetAchievementsForTitleIdResult(ab, &achievementsResultHandle);
        XBOX_LIVE_LOG("XblAchievementsGetAchievementsForTitleIdResult");
    }
    else
    {
        // Get next page results
        result = XblAchievementsResultGetNextResult(ab, &achievementsResultHandle);
        XBOX_LIVE_LOG("XblAchievementsResultGetNextResult");
    }
    if (FAILED(result))
    {
        achievementsContext->Failed = true;
        achievementsContext->Active = false;
        return;
    }

    // Get achievements from the current page
    const XblAchievement* achievements = nullptr;
    size_t achievementsCount = 0;
    result = XblAchievementsResultGetAchievements(achievementsResultHandle, &achievements, &achievementsCount);
    XBOX_LIVE_LOG("XblAchievementsResultGetAchievements");
    if (SUCCEEDED(result))
    {
        // Extract achievements data
        auto& resultAchievements = *achievementsContext->Achievements;
        int32 achievementsStart = resultAchievements.Count();
        resultAchievements.Resize(achievementsStart + (int32)achievementsCount);
        for (size_t i = 0; i < achievementsCount; i++)
            XblGetAchievement(achievements[i], resultAchievements[(int32)(achievementsStart + i)]);
    }

    // Check if has more results to process
    bool hasNextPage = false;
    result = XblAchievementsResultHasNext(achievementsResultHandle, &hasNextPage);
    XBOX_LIVE_LOG("XblAchievementsResultHasNext");
    if (SUCCEEDED(result))
    {
        if (hasNextPage)
        {
            // Go to the next page
            achievementsContext->Iteration++;
            XblAchievementsResultGetNextAsync(achievementsResultHandle, 1, ab);
        }
        else
        {
            // Done
            achievementsContext->Failed = false;
            achievementsContext->Active = false;
        }
        XblAchievementsResultCloseHandle(achievementsResultHandle);
        return;
    }

    // Failed
    achievementsContext->Failed = true;
    achievementsContext->Active = false;
    XblAchievementsResultCloseHandle(achievementsResultHandle);
}

void XblGetStat(const XblStatistic& statistic, float& result)
{
    const StringAnsiView type(statistic.statisticType);
    result = 0.0f;
    if (type == "Int32")
    {
        int32 value;
        if (!StringUtils::Parse(statistic.value, &value))
            result = (float)value;
    }
    else if (type == "Int64")
    {
        int64 value;
        if (!StringUtils::Parse(statistic.value, &value))
            result = (float)value;
    }
    else if (type == "UInt32")
    {
        uint32 value;
        if (!StringUtils::Parse(statistic.value, &value))
            result = (float)value;
    }
    else if (type == "UInt64")
    {
        uint64 value;
        if (!StringUtils::Parse(statistic.value, &value))
            result = (float)value;
    }
    else if (type == "Float")
    {
        float value;
        if (!StringUtils::Parse(statistic.value, &value))
            result = value;
    }
    else if (type == "Double")
    {
        float value;
        if (!StringUtils::Parse(statistic.value, &value))
            result = value;
    }
    else if (type == "Bool")
    {
        const StringAnsiView value(statistic.value);
        result = (value == "1" || value.Compare(StringAnsiView("true"), StringSearchCase::IgnoreCase) == 0) ? 1.0f : 0.0f;
    }
}

void CALLBACK OnGetStat(_In_ XAsyncBlock* ab)
{
    XblStatsContext* statsContext = (XblStatsContext*)ab->context;
    Array<uint8_t> buffer;
    size_t size = 0;
    HRESULT result = XblUserStatisticsGetSingleUserStatisticResultSize(ab, &size);
    XBOX_LIVE_LOG("XblUserStatisticsGetSingleUserStatisticResultSize");
    if (SUCCEEDED(result))
    {
        buffer.Resize((int32)size);
        XblUserStatisticsResult* statisticsResult = nullptr;
        XblUserStatisticsGetSingleUserStatisticResult(ab, size, buffer.Get(), &statisticsResult, &size);
        XBOX_LIVE_LOG("XblUserStatisticsGetSingleUserStatisticResult");
        if (SUCCEEDED(result) && statisticsResult && statisticsResult->serviceConfigStatisticsCount > 0 && statisticsResult->serviceConfigStatistics[0].statisticsCount > 0)
        {
            // Get statistic value
            XblGetStat(statisticsResult->serviceConfigStatistics[0].statistics[0], statsContext->Value);
            statsContext->Failed = false;
            statsContext->Active = false;
            return;
        }
    }

    // Failed
    statsContext->Failed = true;
    statsContext->Active = false;
}

void CALLBACK OnGetPresence(_In_ XAsyncBlock* ab)
{
    XblPresenceContext* presenceContext = (XblPresenceContext*)ab->context;
    XblPresenceRecordHandle presenceRecord;
    HRESULT result = XblPresenceGetPresenceResult(ab, &presenceRecord);
    XBOX_LIVE_LOG("XblPresenceGetPresenceResult");
    if (SUCCEEDED(result))
    {
        XblPresenceUserState userState;
        result = XblPresenceRecordGetUserState(presenceRecord, &userState);
        XBOX_LIVE_LOG("XblPresenceRecordGetUserState");
        if (SUCCEEDED(result))
        {
            switch (userState)
            {
            case XblPresenceUserState::Away:
                presenceContext->Presence = OnlinePresenceStates::Away;
                break;
            case XblPresenceUserState::Offline:
                presenceContext->Presence = OnlinePresenceStates::Offline;
                break;
            default:
                presenceContext->Presence = OnlinePresenceStates::Online;
                break;
            }
        }
        XblPresenceRecordCloseHandle(presenceRecord);
        presenceContext->Failed = false;
        presenceContext->Active = false;
        return;
    }

    // Failed
    presenceContext->Failed = true;
    presenceContext->Active = false;
}

void CALLBACK OnGetFriendsIds(_In_ XAsyncBlock* ab)
{
    XblFriendsContext* friendsContext = (XblFriendsContext*)ab->context;
    XblSocialRelationshipResultHandle socialRelationship;
    HRESULT result;
    if (friendsContext->Iteration == 0)
    {
        // Get results
        result = XblSocialGetSocialRelationshipsResult(ab, &socialRelationship);
        XBOX_LIVE_LOG("XblSocialGetSocialRelationshipsResult");
    }
    else
    {
        // Get next page results
        result = XblSocialRelationshipResultGetNextResult(ab, &socialRelationship);
        XBOX_LIVE_LOG("XblSocialRelationshipResultGetNextResult");
    }
    if (FAILED(result))
    {
        friendsContext->Failed = true;
        friendsContext->Active = false;
        return;
    }

    // Get relationships from the current page
    const XblSocialRelationship* relationships = nullptr;
    size_t relationshipsCount = 0;
    result = XblSocialRelationshipResultGetRelationships(socialRelationship, &relationships, &relationshipsCount);
    XBOX_LIVE_LOG("XblSocialRelationshipResultGetRelationships");
    if (SUCCEEDED(result))
    {
        // Extract relationships data
        auto& resultFriends = friendsContext->FriendsIds;
        int32 friendsStart = resultFriends.Count();
        resultFriends.Resize(friendsStart + (int32)relationshipsCount);
        for (size_t i = 0; i < relationshipsCount; i++)
            resultFriends[(int32)(friendsStart + i)] = relationships[i].xboxUserId;
    }

    // Check if has more results to process
    bool hasNextPage = false;
    result = XblSocialRelationshipResultHasNext(socialRelationship, &hasNextPage);
    XBOX_LIVE_LOG("XblSocialRelationshipResultHasNext");
    if (SUCCEEDED(result))
    {
        if (hasNextPage)
        {
            // Go to the next page
            friendsContext->Iteration++;
            XblSocialRelationshipResultGetNextAsync(friendsContext->Context, socialRelationship, 0, ab);
        }
        else
        {
            // Done
            friendsContext->Failed = false;
            friendsContext->Active = false;
        }
        XblSocialRelationshipResultCloseHandle(socialRelationship);
        return;
    }

    // Failed
    friendsContext->Failed = true;
    friendsContext->Active = false;
    XblSocialRelationshipResultCloseHandle(socialRelationship);
}

void CALLBACK OnGetFriendsProfiles(_In_ XAsyncBlock* ab)
{
    XblFriendsContext* friendsContext = (XblFriendsContext*)ab->context;
    size_t profileCount;
    HRESULT result = XblProfileGetUserProfilesResultCount(ab, &profileCount);
    XBOX_LIVE_LOG("XblProfileGetUserProfilesResultCount");
    if (SUCCEEDED(result))
    {
        Array<XblUserProfile> profiles;
        profiles.Resize((int32)profileCount);
        result = XblProfileGetUserProfilesResult(ab, profileCount, profiles.Get());
        XBOX_LIVE_LOG("XblProfileGetUserProfilesResult");
        if (SUCCEEDED(result))
        {
            friendsContext->Friends->Resize(profiles.Count());
            for (int32 i = 0; i < profiles.Count(); i++)
            {
                const XblUserProfile& profile = profiles[i];
                OnlineUser& f = friendsContext->Friends->At(i);
                f.Id = GetUserId(profile.xboxUserId);
                f.Name = profile.modernGamertag;
                // TODO: query presence for friends
                f.PresenceState = OnlinePresenceStates::Online;
            }
        }

        // Done
        friendsContext->Failed = false;
        friendsContext->Active = false;
        return;
    }

    // Failed
    friendsContext->Failed = true;
    friendsContext->Active = false;
}

OnlinePlatformXboxLive::OnlinePlatformXboxLive(const SpawnParams& params)
    : ScriptingObject(params)
{
}

bool OnlinePlatformXboxLive::Initialize()
{
    // Initialize
    uint32_t titleId = 0;
    HRESULT result = S_OK;
    result = XGameGetXboxTitleId(&titleId);
    XBOX_LIVE_CHECK_RETURN("XGameGetXboxTitleId");
    char sandboxId[XSystemXboxLiveSandboxIdMaxBytes] = {};
    result = XSystemGetXboxLiveSandboxId(XSystemXboxLiveSandboxIdMaxBytes, sandboxId, nullptr);
    XBOX_LIVE_LOG("XSystemGetXboxLiveSandboxId");
    LOG(Info, "Initializing Xbox Live with TitleId={0}, SandboxId={1}", titleId, String(sandboxId));
    if (FAILED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::Manual, &_taskQueue)))
        return true;
    XblMemSetFunctions(XblMemAlloc, XblMemFree);
    const auto settings = PlatformSettings::Get();
    XblInitArgs xblArgs = {};
    xblArgs.queue = _taskQueue;
    if (settings->SCID.HasChars())
        xblArgs.scid = settings->SCID.Get();
    else
        xblArgs.scid = "00000000-0000-0000-0000-000000000000";
    result = XblInitialize(&xblArgs);
    XBOX_LIVE_CHECK_RETURN("XblInitialize");
    _titleId = titleId;
    Engine::LateUpdate.Bind<OnlinePlatformXboxLive, &OnlinePlatformXboxLive::OnUpdate>(this);

    return false;
}

void OnlinePlatformXboxLive::Deinitialize()
{
    for (const auto& e : _gameSaveProviders)
        XGameSaveCloseProvider(e.Value);
    _gameSaveProviders.Clear();
    for (const auto& e : _users)
        XblContextCloseHandle(e.Value);
    _users.Clear();
    Engine::LateUpdate.Unbind<OnlinePlatformXboxLive, &OnlinePlatformXboxLive::OnUpdate>(this);
    static XAsyncBlock emptyBlock{ nullptr, nullptr, nullptr };
    XblCleanupAsync(&emptyBlock);
    if (_taskQueue)
    {
        XTaskQueueCloseHandle(_taskQueue);
        _taskQueue = nullptr;
    }
}

bool OnlinePlatformXboxLive::UserLogin(User* localUser)
{
    if (Platform::Users.Count() == 0)
        return true;
    if (!localUser)
        localUser = Platform::Users.First();
    if (_users.ContainsKey(localUser))
        return false;
    XblContextHandle context;
    HRESULT result = XblContextCreateHandle(localUser->UserHandle, &context);
    XBOX_LIVE_CHECK_RETURN("XblContextCreateHandle");
    _users[localUser] = context;
    return false;
}

bool OnlinePlatformXboxLive::UserLogout(User* localUser)
{
    if (Platform::Users.Count() == 0)
        return true;
    if (!localUser)
        localUser = Platform::Users.First();
    XblContextHandle context;
    if (_users.TryGet(localUser, context))
    {
        if (const XGameSaveProviderHandle* provider = _gameSaveProviders.TryGet(localUser))
        {
            XGameSaveCloseProvider(*provider);
            _gameSaveProviders.Remove(localUser);
        }
        XblContextCloseHandle(context);
        _users.Remove(localUser);
    }
    return false;
}

bool OnlinePlatformXboxLive::GetUserLoggedIn(User* localUser)
{
    return _users.ContainsKey(localUser);
}

bool OnlinePlatformXboxLive::GetUser(OnlineUser& user, User* localUser)
{
    XblContextHandle context;
    if (GetContext(localUser, context))
    {
        uint64_t xboxUserId;
        XblContextGetXboxUserId(context, &xboxUserId);
        user.Id = GetUserId(xboxUserId);

        char gamerTag[XUserGamertagComponentModernMaxBytes];
        size_t gamerTagSize;
        XUserGetGamertag(localUser->UserHandle, XUserGamertagComponent::Modern, ARRAY_COUNT(gamerTag), gamerTag, &gamerTagSize);
        user.Name.SetUTF8(gamerTag, StringUtils::Length(gamerTag));

        XblPresenceContext presenceContext;
        presenceContext.Presence = OnlinePresenceStates::Online;
        XAsyncBlock ab;
        ab.queue = _taskQueue;
        ab.callback = OnGetPresence;
        ab.context = &presenceContext;
        HRESULT result = XblPresenceGetPresenceAsync(context, xboxUserId, &ab);
        if (SUCCEEDED(result))
            XblSyncWait(presenceContext, _taskQueue);
        else
            XBOX_LIVE_LOG("XblPresenceGetPresenceAsync");
        user.PresenceState = presenceContext.Presence;
    }
    return true;
}

bool OnlinePlatformXboxLive::GetFriends(Array<OnlineUser>& friends, User* localUser)
{
    XblContextHandle context;
    if (GetContext(localUser, context))
    {
        // Query friends list (only IDs)
        uint64_t xboxUserId;
        XblContextGetXboxUserId(context, &xboxUserId);
        XblFriendsContext friendsContext;
        friendsContext.Context = context;
        friendsContext.Friends = &friends;
        XAsyncBlock ab;
        ab.queue = _taskQueue;
        ab.callback = OnGetFriendsIds;
        ab.context = &friendsContext;
        HRESULT result = XblSocialGetSocialRelationshipsAsync(context, xboxUserId, XblSocialRelationshipFilter::All, 0, 0, &ab);
        XBOX_LIVE_CHECK_RETURN("XblSocialGetSocialRelationshipsAsync");
        if (XblSyncWait(friendsContext, _taskQueue))
            return true;

        // No friends, nobody likes you
        if (friendsContext.FriendsIds.IsEmpty())
            return false;

        // Query info for all friends
        ab.callback = OnGetFriendsProfiles;
        result = XblProfileGetUserProfilesAsync(context, friendsContext.FriendsIds.Get(), friendsContext.FriendsIds.Count(), &ab);
        XBOX_LIVE_CHECK_RETURN("XblProfileGetUserProfilesAsync");
        return XblSyncWait(friendsContext, _taskQueue);
    }
    return true;
}

bool OnlinePlatformXboxLive::GetAchievements(Array<OnlineAchievement>& achievements, User* localUser)
{
    XblContextHandle context;
    if (GetContext(localUser, context))
    {
        uint64_t xboxUserId;
        XblContextGetXboxUserId(context, &xboxUserId);
        XblAchievementsContext achievementsContext;
        achievementsContext.Achievements = &achievements;
        XAsyncBlock ab;
        ab.queue = _taskQueue;
        ab.callback = OnGetAchievements;
        ab.context = &achievementsContext;
        HRESULT result = XblAchievementsGetAchievementsForTitleIdAsync(context, xboxUserId, _titleId, XblAchievementType::All, false, XblAchievementOrderBy::DefaultOrder, 0, 0, &ab);
        XBOX_LIVE_CHECK_RETURN("XblAchievementsGetAchievementsForTitleIdAsync");
        return XblSyncWait(achievementsContext, _taskQueue);
    }
    return true;
}

bool OnlinePlatformXboxLive::UnlockAchievement(const StringView& name, User* localUser)
{
    return UnlockAchievementProgress(name, 100.0f, localUser);
}

bool OnlinePlatformXboxLive::UnlockAchievementProgress(const StringView& name, float progress, User* localUser)
{
    XblContextHandle context;
    if (GetContext(localUser, context))
    {
        uint64_t xboxUserId;
        XblContextGetXboxUserId(context, &xboxUserId);
        const StringAsANSI<> nameStr(name.Get(), name.Length());
        XAsyncBlock ab;
        ab.queue = _taskQueue;
        ab.callback = nullptr;
        HRESULT result = XblAchievementsUpdateAchievementAsync(context, xboxUserId, nameStr.Get(), (uint32_t)progress, &ab);
        if (result == HTTP_E_STATUS_NOT_MODIFIED)
            return false;
        XBOX_LIVE_CHECK_RETURN("XblAchievementsUpdateAchievementAsync");
        return false;
    }
    return true;
}

#if !BUILD_RELEASE

bool OnlinePlatformXboxLive::ResetAchievements(User* localUser)
{
    // Not supported
    return true;
}

#endif

bool OnlinePlatformXboxLive::GetStat(const StringView& name, float& value, User* localUser)
{
    XblContextHandle context;
    if (GetContext(localUser, context))
    {
        uint64_t xboxUserId;
        XblContextGetXboxUserId(context, &xboxUserId);
        XblStatsContext statsContext;
        statsContext.Value = value;
        XAsyncBlock ab;
        ab.queue = _taskQueue;
        ab.callback = OnGetStat;
        ab.context = &statsContext;
        const char* scid = nullptr;
        XblGetScid(&scid);
        const StringAsANSI<> nameStr(name.Get(), name.Length());
        HRESULT result = XblUserStatisticsGetSingleUserStatisticAsync(context, xboxUserId, scid, nameStr.Get(), &ab);
        XBOX_LIVE_CHECK_RETURN("XblUserStatisticsGetSingleUserStatisticAsync");
        return XblSyncWait(statsContext, _taskQueue);
    }
    return true;
}

bool OnlinePlatformXboxLive::SetStat(const StringView& name, float value, User* localUser)
{
    XblContextHandle context;
    if (GetContext(localUser, context))
    {
        XAsyncBlock ab;
        ab.queue = _taskQueue;
        ab.callback = nullptr;
        const StringAsANSI<> nameStr(name.Get(), name.Length());
        XblTitleManagedStatistic statistic;
        statistic.statisticName = nameStr.Get();
        statistic.statisticType = XblTitleManagedStatType::Number;
        statistic.numberValue = (double)value;
        statistic.stringValue = nullptr;
        HRESULT result = XblTitleManagedStatsUpdateStatsAsync(context, &statistic, 1, &ab);
        XBOX_LIVE_CHECK_RETURN("XblTitleManagedStatsUpdateStatsAsync");
        return false;
    }
    return true;
}

bool OnlinePlatformXboxLive::GetSaveGame(const StringView& name, Array<byte>& data, User* localUser)
{
    XGameSaveProviderHandle provider;
    if (GetSaveGameProvider(localUser, provider))
    {
        data.Clear();
        const StringAsANSI<> containerName(name.Get(), name.Length());

        // Check if savegame exists
        bool exists = false;
        HRESULT result = XGameSaveGetContainerInfo(provider, containerName.Get(), &exists, [](const XGameSaveContainerInfo* containerInfo, void* context)
        {
            *(bool*)context = true;
            return true;
        });
        XBOX_LIVE_CHECK_RETURN("XGameSaveGetContainerInfo");
        if (exists)
        {
            // Get container
            XGameSaveContainerHandle container = nullptr;
            result = XGameSaveCreateContainer(provider, containerName.Get(), &container);
            XBOX_LIVE_LOG("XGameSaveCreateContainer");
            if (SUCCEEDED(result))
            {
                // Find blob size
                int32 blobSize = 0;
                result = XGameSaveEnumerateBlobInfo(container, &blobSize, [](const XGameSaveBlobInfo* blobInfo, void* context)
                {
                    if (StringAnsiView(blobInfo->name) == XBOX_LIVE_SAVE_GAME_BLOB_NAME)
                    {
                        *(int32*)context = blobInfo->size;
                        return false;
                    }
                    return true;
                });
                XBOX_LIVE_LOG("XGameSaveEnumerateBlobInfo");
                if (SUCCEEDED(result) && blobSize > 0)
                {
                    // Get blob data
                    const char* blobNames[] = { XBOX_LIVE_SAVE_GAME_BLOB_NAME };
                    uint32_t blobCount = 1;
                    uint32_t blobsSize = sizeof(XGameSaveBlob) + StringUtils::Length(XBOX_LIVE_SAVE_GAME_BLOB_NAME) + 1 + blobSize;
                    XGameSaveBlob* blobs = (XGameSaveBlob*)Allocator::Allocate(blobsSize);
                    result = XGameSaveReadBlobData(container, blobNames, &blobCount, blobsSize, blobs);
                    XBOX_LIVE_LOG("XGameSaveReadBlobData");
                    if (SUCCEEDED(result))
                    {
                        // Set result data
                        data.Set(blobs->data, (int32)blobs->info.size);
                    }
                    Allocator::Free(blobs);
                }
                XGameSaveCloseContainer(container);
            }
        }
        return false;
    }
    return true;
}

bool OnlinePlatformXboxLive::SetSaveGame(const StringView& name, const Span<byte>& data, User* localUser)
{
    XGameSaveProviderHandle provider;
    if (GetSaveGameProvider(localUser, provider))
    {
        const StringAsANSI<> containerName(name.Get(), name.Length());

        // Get or create container
        XGameSaveContainerHandle container = nullptr;
        HRESULT result = XGameSaveCreateContainer(provider, containerName.Get(), &container);
        XBOX_LIVE_LOG("XGameSaveCreateContainer");

        // Submit or delete blob
        XGameSaveUpdateHandle update = nullptr;
        if (SUCCEEDED(result))
        {
            result = XGameSaveCreateUpdate(container, containerName.Get(), &update);
            XBOX_LIVE_LOG("XGameSaveCreateUpdate");
        }
        if (SUCCEEDED(result))
        {
            if (data.Length() > 0)
            {
                result = XGameSaveSubmitBlobWrite(update, XBOX_LIVE_SAVE_GAME_BLOB_NAME, data.Get(), data.Length());
                XBOX_LIVE_LOG("XGameSaveSubmitBlobWrite");
            }
            else
            {
                result = XGameSaveSubmitBlobDelete(update, XBOX_LIVE_SAVE_GAME_BLOB_NAME);
                XBOX_LIVE_LOG("XGameSaveSubmitBlobDelete");
            }
        }
        if (SUCCEEDED(result))
        {
            result = XGameSaveSubmitUpdate(update);
            XBOX_LIVE_LOG("XGameSaveSubmitUpdate");
        }

        // Finalize
        if (update)
            XGameSaveCloseUpdate(update);
        if (container)
            XGameSaveCloseContainer(container);
        return FAILED(result);
    }
    return true;
}

bool OnlinePlatformXboxLive::GetSaveGameProvider(User*& localUser, XGameSaveProvider*& provider)
{
    if (Platform::Users.Count() == 0)
        return false;
    if (!localUser)
        localUser = Platform::Users.First();
    if (_gameSaveProviders.TryGet(localUser, provider))
        return true;

    // Initialize gamesave provider for this user
    const char* scid = nullptr;
    XblGetScid(&scid);
    XAsyncBlock ab;
    ab.queue = _taskQueue;
    ab.callback = nullptr;
    HRESULT result = XGameSaveInitializeProviderAsync(localUser->UserHandle, scid, true, &ab);
    XBOX_LIVE_CHECK_RETURN("XGameSaveInitializeProviderAsync");
    if (SUCCEEDED(result) && !XblSyncWait(ab, _taskQueue))
    {
        result = XGameSaveInitializeProviderResult(&ab, &provider);
        XBOX_LIVE_CHECK_RETURN("XGameSaveInitializeProviderResult");
        if (SUCCEEDED(result))
        {
            // Cache provider for this user
            _gameSaveProviders.Add(localUser, provider);
            return true;
        }
    }

    return false;
}

bool OnlinePlatformXboxLive::GetContext(User*& localUser, XblContext*& context) const
{
    if (Platform::Users.Count() == 0)
        return false;
    if (!localUser)
        localUser = Platform::Users.First();
    return _users.TryGet(localUser, context);
}

void OnlinePlatformXboxLive::OnUpdate()
{
    // Flush task queue events
    while (XTaskQueueDispatch(_taskQueue, XTaskQueuePort::Completion, 0))
    {
    }
}

#endif
