#include "BattlegroundInvitationsMgr.hpp"
#include "BattlegroundMgr.h"
#include "BattlegroundPacketFactory.hpp"

namespace MS
{
    namespace Battlegrounds
    {
        BattlegroundInvitationsMgr::BattlegroundInvitationsMgr()
        {
            for (uint32 i = 0; i < BG_TEAMS_COUNT; ++i)
            {
                for (uint32 j = 0; j < Brackets::Count; ++j)
                {
                    m_SumOfWaitTimes[i][j] = 0;
                    m_WaitTimeLastPlayer[i][j] = 0;
                    for (uint32 k = 0; k < COUNT_OF_PLAYERS_TO_AVERAGE_WAIT_TIME; ++k)
                        m_WaitTimes[i][j][k] = 0;
                }
            }
        }

        bool BattlegroundInvitationsMgr::InviteGroupToBG(GroupQueueInfo* p_GroupInfo, Battleground* p_Bg, uint32 p_Side)
        {
            // Set side if needed.
            if (p_Side)
                p_GroupInfo->m_Team = p_Side;

            if (!p_GroupInfo->m_IsInvitedToBGInstanceGUID)
            {
                // Not yet invited, set invitation.
                p_GroupInfo->m_IsInvitedToBGInstanceGUID = p_Bg->GetInstanceID();
                BattlegroundTypeId l_BGTypeId = p_Bg->GetTypeID();
                BattlegroundType::Type l_BgQueueTypeId = GetTypeFromId(l_BGTypeId, p_Bg->GetArenaType(), p_GroupInfo->m_IsSkirmish);
                Bracket::Id l_BracketId = p_Bg->GetBracketId();

                p_GroupInfo->m_RemoveInviteTime = getMSTime() + INVITE_ACCEPT_WAIT_TIME;

                // Loop through the players and send them the inviting packet.
                for (auto l_Itr = std::begin(p_GroupInfo->m_Players); l_Itr != std::end(p_GroupInfo->m_Players); ++l_Itr)
                {
                    /// Get the player.
                    Player* l_Player = ObjectAccessor::FindPlayer(l_Itr->first);

                    /// If offline, skip him, this should not happen - player is removed from queue when he logs out.
                    if (!l_Player)
                        continue;

                    /// Insert player in the invited map players.
                    PlayerQueueInfo& l_PlayerQueue = m_InvitedPlayers[l_Player->GetGUID()];
                    l_PlayerQueue.LastOnlineTime = getMSTime();
                    l_PlayerQueue.GroupInfo = p_GroupInfo;

                    /// Invite the player.
                    UpdateAverageWaitTimeForGroup(p_GroupInfo, l_BracketId);

                    /// Set invited player counters.
                    p_Bg->IncreaseInvitedCount(p_GroupInfo->m_Team);


                    l_Player->SetInviteForBattlegroundQueueType(p_GroupInfo->m_BgTypeId, p_GroupInfo->m_IsInvitedToBGInstanceGUID);

                    /// Create remind invite news.
                    BGQueueInviteEvent* l_InviteEvent = new BGQueueInviteEvent(l_Player->GetGUID(), p_GroupInfo->m_IsInvitedToBGInstanceGUID, l_BGTypeId, p_GroupInfo->m_ArenaType, p_GroupInfo->m_RemoveInviteTime);
                    m_Events.AddEvent(l_InviteEvent, m_Events.CalculateTime(INVITATION_REMIND_TIME));

                    /// Create automatic remove events.
                    BGQueueRemoveEvent* l_RemoveEvent = new BGQueueRemoveEvent(l_Player->GetGUID(), p_GroupInfo->m_IsInvitedToBGInstanceGUID, l_BGTypeId, l_BgQueueTypeId, p_GroupInfo->m_RemoveInviteTime);
                    m_Events.AddEvent(l_RemoveEvent, m_Events.CalculateTime(INVITE_ACCEPT_WAIT_TIME));

                    uint32 l_QueueSlot = l_Player->GetBattlegroundQueueIndex(p_GroupInfo->m_BgTypeId == BattlegroundType::RandomBattleground ? BattlegroundType::RandomBattleground : l_BgQueueTypeId);

                    sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: invited player %s (%u) to BG instance %u queueindex %u bgtype %u, I can't help it if they don't press the enter battle button.",
                        l_Player->GetName(), l_Player->GetGUIDLow(), p_Bg->GetInstanceID(), l_QueueSlot, p_Bg->GetTypeID());

                    /// Send status packet.
                    WorldPacket data;
                    MS::Battlegrounds::PacketFactory::Status(&data, p_Bg, l_Player, l_QueueSlot, STATUS_WAIT_JOIN, INVITE_ACCEPT_WAIT_TIME, l_Player->GetBattlegroundQueueJoinTime(l_BGTypeId), p_GroupInfo->m_ArenaType, p_GroupInfo->m_IsSkirmish);
                    l_Player->GetSession()->SendPacket(&data);
                }

                return true;
            }

            return false;
        }

        void BattlegroundInvitationsMgr::UpdateAverageWaitTimeForGroup(GroupQueueInfo* p_GroupInfo, Bracket::Id p_BracketId)
        {
            uint32 l_TimeInQueue = getMSTimeDiff(p_GroupInfo->m_JoinTime, getMSTime());
            uint8 l_TeamIndex = BG_TEAM_ALLIANCE;                    //default set to BG_TEAM_ALLIANCE - or non rated arenas!

            if (!p_GroupInfo->m_ArenaType)
            {
                if (p_GroupInfo->m_Team == HORDE)
                    l_TeamIndex = BG_TEAM_HORDE;
            }
            else
            {
                if (p_GroupInfo->m_IsRatedBG)
                    l_TeamIndex = BG_TEAM_HORDE;                     //for rated arenas use BG_TEAM_HORDE
            }

            /// Store pointer to arrayindex of player that was added first.
            uint32* lastPlayerAddedPointer = &(m_WaitTimeLastPlayer[l_TeamIndex][p_BracketId]);

            /// Remove his sum of sum.
            m_SumOfWaitTimes[l_TeamIndex][p_BracketId] -= m_WaitTimes[l_TeamIndex][p_BracketId][(*lastPlayerAddedPointer)];

            /// Set average time to new.
            m_WaitTimes[l_TeamIndex][p_BracketId][(*lastPlayerAddedPointer)] = l_TimeInQueue;

            /// Add new time to sum.
            m_SumOfWaitTimes[l_TeamIndex][p_BracketId] += l_TimeInQueue;

            /// Set index of last player added to next one.
            (*lastPlayerAddedPointer)++;
            (*lastPlayerAddedPointer) %= COUNT_OF_PLAYERS_TO_AVERAGE_WAIT_TIME;
        }

        uint32 BattlegroundInvitationsMgr::GetAverageQueueWaitTime(GroupQueueInfo* p_GroupInfo, Bracket::Id p_BracketId) const
        {
            /// Default set to BG_TEAM_ALLIANCE - or non rated arenas!
            uint8 l_TeamIndex = BG_TEAM_ALLIANCE;

            if (!p_GroupInfo->m_ArenaType)
            {
                if (p_GroupInfo->m_Team == HORDE)
                    l_TeamIndex = BG_TEAM_HORDE;
            }
            else
            {
                if (p_GroupInfo->m_IsRatedBG)
                    l_TeamIndex = BG_TEAM_HORDE; /// For rated arenas use BG_TEAM_HORDE.
            }

            /// Check if there is enough values (we always add values > 0).
            if (m_WaitTimes[l_TeamIndex][p_BracketId][COUNT_OF_PLAYERS_TO_AVERAGE_WAIT_TIME - 1])
                return (m_SumOfWaitTimes[l_TeamIndex][p_BracketId] / COUNT_OF_PLAYERS_TO_AVERAGE_WAIT_TIME);

            /// If there aren't enough values return 0 - not available.
            return 0;
        }

        bool BattlegroundInvitationsMgr::IsOwningPlayer(uint64 p_Guid) const
        {
            return m_InvitedPlayers.find(p_Guid) != std::cend(m_InvitedPlayers);
        }

        bool BattlegroundInvitationsMgr::IsPlayerInvited(uint64 p_PlrGuid, uint32 p_BgInstanceId, uint32 p_RemoveTime) const
        {
            QueuedPlayersMap::const_iterator l_Itr = m_InvitedPlayers.find(p_PlrGuid);

            return (l_Itr != std::cend(m_InvitedPlayers)
                && l_Itr->second.GroupInfo->m_IsInvitedToBGInstanceGUID == p_BgInstanceId
                && l_Itr->second.GroupInfo->m_RemoveInviteTime == p_RemoveTime);
        }

        void BattlegroundInvitationsMgr::UpdateEvents(uint32 diff)
        {
            m_Events.Update(diff);
        }

        void BattlegroundInvitationsMgr::RemovePlayer(uint64 p_Guid, bool p_DecreaseInvitedCount)
        {
            int32 l_BracketId = -1;                                     // signed for proper for-loop finish

            /// Remove player from map, if he's there.
            auto l_Itr = m_InvitedPlayers.find(p_Guid);
            if (l_Itr == m_InvitedPlayers.end())
                return;

            GroupQueueInfo* l_Group = l_Itr->second.GroupInfo;

            //player can't be in queue without group, but just in case
            if (l_BracketId == -1)
            {
                sLog->outError(LOG_FILTER_BATTLEGROUND, "BattlegroundQueue: ERROR Cannot find groupinfo for player GUID: %u", GUID_LOPART(p_Guid));
                return;
            }
            sLog->outDebug(LOG_FILTER_BATTLEGROUND, "BattlegroundQueue: Removing player GUID %u, from bracket_id %u", GUID_LOPART(p_Guid), (uint32)l_BracketId);

            // ALL variables are correctly set
            // We can ignore leveling up in queue - it should not cause crash
            // remove player from group
            // if only one player there, remove group

            /// Remove player queue info from group queue info.
            auto pitr = l_Group->m_Players.find(p_Guid);
            if (pitr != l_Group->m_Players.end())
                l_Group->m_Players.erase(pitr);

            // if invited to bg, and should decrease invited count, then do it
            if (p_DecreaseInvitedCount && l_Group->m_IsInvitedToBGInstanceGUID)
                if (Battleground* bg = sBattlegroundMgr->GetBattleground(l_Group->m_IsInvitedToBGInstanceGUID, l_Group->m_BgTypeId == BattlegroundType::AllArenas ? BattlegroundType::None : l_Group->m_BgTypeId))
                    bg->DecreaseInvitedCount(l_Group->m_Team);

            /// Remove player queue info.
            m_InvitedPlayers.erase(l_Itr);


            // if player leaves queue and he is invited to rated arena match, then he have to lose
            if (l_Group->m_IsInvitedToBGInstanceGUID && l_Group->m_IsSkirmish && p_DecreaseInvitedCount)
            {
                if (Player* player = ObjectAccessor::FindPlayer(p_Guid))
                {
                    // Update personal rating
                    uint8 slot = Arena::GetSlotByType(l_Group->m_ArenaType);
                    int32 mod = Arena::GetRatingMod(player->GetArenaPersonalRating(slot), l_Group->m_OpponentsMatchmakerRating, false);
                    player->SetArenaPersonalRating(slot, player->GetArenaPersonalRating(slot) + mod);

                    // Update matchmaker rating
                    player->SetArenaMatchMakerRating(slot, player->GetArenaMatchMakerRating(slot) - 12);

                    // Update personal played stats
                    player->IncrementWeekGames(slot);
                    player->IncrementSeasonGames(slot);
                }
            }

            // remove group queue info if needed
            /*if (l_Group->m_Players.empty())
            {
                m_QueuedGroups[l_BracketId][l_Index].erase(group_itr);
                delete l_Group;
            }*/

            // if group wasn't empty, so it wasn't deleted, and player have left a rated
            // queue -> everyone from the group should leave too
            // don't remove recursively if already invited to bg!
            else if (!l_Group->m_IsInvitedToBGInstanceGUID && l_Group->m_IsRatedBG)
            {
                // remove next player, this is recursive
                // first send removal information
                if (Player* plr2 = ObjectAccessor::FindPlayer(l_Group->m_Players.begin()->first))
                {
                    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(l_Group->m_BgTypeId);
                    BattlegroundType::Type bgQueueTypeId = GetTypeFromId(GetIdFromType(l_Group->m_BgTypeId), l_Group->m_ArenaType);
                    uint32 queueSlot = plr2->GetBattlegroundQueueIndex(bgQueueTypeId);
                    plr2->RemoveBattlegroundQueueId(bgQueueTypeId); // must be called this way, because if you move this call to
                    // queue->removeplayer, it causes bugs
                    WorldPacket data;
                    PacketFactory::Status(&data, bg, plr2, queueSlot, STATUS_NONE, plr2->GetBattlegroundQueueJoinTime(l_Group->m_BgTypeId), 0, 0, false);
                    plr2->GetSession()->SendPacket(&data);
                }
                // then actually delete, this may delete the group as well!
                RemovePlayer(l_Group->m_Players.begin()->first, p_DecreaseInvitedCount);
            }
        }

        //////////////////////////////////////////////////////////////////////////
        /// Battleground Queue Events
        //////////////////////////////////////////////////////////////////////////

        bool BGQueueInviteEvent::Execute(uint64 /*e_time*/, uint32 /*p_time*/)
        {
            Player* l_Player = ObjectAccessor::FindPlayer(m_PlayerGuid);
            // player logged off (we should do nothing, he is correctly removed from queue in another procedure)
            if (!l_Player)
                return true;

            Battleground* l_Bg = sBattlegroundMgr->GetBattleground(m_BgInstanceGUID, GetSchedulerType(m_BgTypeId));
            //if battleground ended and its instance deleted - do nothing
            if (!l_Bg)
                return true;

            BattlegroundType::Type bgQueueTypeId = GetTypeFromId(l_Bg->GetTypeID(), l_Bg->GetArenaType(), l_Bg->IsSkirmish());
            uint32 l_QueueSlot = l_Player->GetBattlegroundQueueIndex(bgQueueTypeId);
            if (l_QueueSlot < PLAYER_MAX_BATTLEGROUND_QUEUES)         // player is in queue or in battleground
            {
                // Check if the player is invited to this battleground.
                BattlegroundInvitationsMgr& l_InvitationsMgr = sBattlegroundMgr->GetInvitationsMgr();
                if (l_InvitationsMgr.IsPlayerInvited(m_PlayerGuid, m_BgInstanceGUID, m_RemoveTime))
                {
                    WorldPacket l_Data;
                    //we must send remaining time in queue
                    PacketFactory::Status(&l_Data, l_Bg, l_Player, l_QueueSlot, STATUS_WAIT_JOIN, INVITE_ACCEPT_WAIT_TIME - INVITATION_REMIND_TIME, l_Player->GetBattlegroundQueueJoinTime(m_BgTypeId), m_ArenaType, false);
                    l_Player->GetSession()->SendPacket(&l_Data);
                }
            }
            return true;                                            //event will be deleted
        }

        void BGQueueInviteEvent::Abort(uint64 /*e_time*/)
        {
            //do nothing
        }

        /*
        this event has many possibilities when it is executed:
        1. player is in battleground (he clicked enter on invitation window)
        2. player left battleground queue and he isn't there any more
        3. player left battleground queue and he joined it again and IsInvitedToBGInstanceGUID = 0
        4. player left queue and he joined again and he has been invited to same battleground again -> we should not remove him from queue yet
        5. player is invited to bg and he didn't choose what to do and timer expired - only in this condition we should call queue::RemovePlayer
        we must remove player in the 5. case even if battleground object doesn't exist!
        */
        bool BGQueueRemoveEvent::Execute(uint64 /*e_time*/, uint32 /*p_time*/)
        {
            Player* l_Player = ObjectAccessor::FindPlayer(m_PlayerGuid);
            if (!l_Player)
                // player logged off (we should do nothing, he is correctly removed from queue in another procedure)
                return true;

            Battleground* l_Bg = sBattlegroundMgr->GetBattleground(m_BgInstanceGUID, GetSchedulerType(m_BgTypeId));
            //battleground can be deleted already when we are removing queue info
            //bg pointer can be NULL! so use it carefully!

            uint32 l_QueueSlot = l_Player->GetBattlegroundQueueIndex(m_BgType);
            if (l_QueueSlot < PLAYER_MAX_BATTLEGROUND_QUEUES)         // player is in queue, or in Battleground
            {
                // check if player is in queue for this BG and if we are removing his invite event
                BattlegroundInvitationsMgr& l_InvitationsMgr = sBattlegroundMgr->GetInvitationsMgr();
                if (l_InvitationsMgr.IsPlayerInvited(m_PlayerGuid, m_BgInstanceGUID, m_RemoveTime))
                {
                    sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Battleground: removing player %u from bg queue for instance %u because of not pressing enter battle in time.", l_Player->GetGUIDLow(), m_BgInstanceGUID);

                    l_Player->RemoveBattlegroundQueueId(m_BgType);
                    l_InvitationsMgr.RemovePlayer(m_PlayerGuid, true);
                    //update queues if battleground isn't ended
                    //if (l_Bg && l_Bg->isBattleground() && l_Bg->GetStatus() != STATUS_WAIT_LEAVE)
                    //    sBattlegroundMgr->ScheduleQueueUpdate(0, 0, m_BgType, m_BgTypeId, l_Bg->GetBracketId());

                    WorldPacket l_Data;
                    PacketFactory::Status(&l_Data, l_Bg, l_Player, l_QueueSlot, STATUS_NONE, l_Player->GetBattlegroundQueueJoinTime(m_BgTypeId), 0, 0, false);
                    l_Player->GetSession()->SendPacket(&l_Data);
                }
            }

            //event will be deleted
            return true;
        }

        void BGQueueRemoveEvent::Abort(uint64 /*e_time*/)
        {
            //do nothing
        }
    }
}