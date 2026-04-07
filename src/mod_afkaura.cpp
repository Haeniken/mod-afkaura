#include "Config.h"
#include "Log.h"
#include "MovementHandlerScript.h"
#include "Player.h"
#include "PlayerScript.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldScript.h"
#include <cstdint>
#include <unordered_map>

namespace
{
    struct AFKAuraConfig
    {
        bool enabled = false;
        uint32 auraSpellId = 0;
        uint32 idleTimeMs = 300000;
        bool ignoreGameMasters = true;
        bool ignoreInCombat = false;
        bool ignoreBattlegrounds = true;
        bool ignoreFlight = true;
        bool setPlayerFlagAFK = false;
    };

    struct AFKAuraState
    {
        uint32 idleMs = 0;
        bool auraApplied = false;
        bool afkFlagApplied = false;
    };

    AFKAuraConfig sAFKAuraConfig;
    std::unordered_map<uint64, AFKAuraState> sAFKAuraStates;

    AFKAuraState& GetState(Player* player)
    {
        return sAFKAuraStates[player->GetGUID().GetRawValue()];
    }

    void RemoveAppliedEffects(Player* player, AFKAuraState& state)
    {
        if (!player)
            return;

        if (state.auraApplied && sAFKAuraConfig.auraSpellId && player->HasAura(sAFKAuraConfig.auraSpellId))
            player->RemoveAurasDueToSpell(sAFKAuraConfig.auraSpellId);

        if (state.afkFlagApplied && player->HasPlayerFlag(PLAYER_FLAGS_AFK))
            player->RemovePlayerFlag(PLAYER_FLAGS_AFK);

        state.auraApplied = false;
        state.afkFlagApplied = false;
    }

    void ResetActivity(Player* player)
    {
        if (!player)
            return;

        AFKAuraState& state = GetState(player);
        state.idleMs = 0;
        RemoveAppliedEffects(player, state);
    }

    void ClearState(Player* player)
    {
        if (!player)
            return;

        uint64 const guid = player->GetGUID().GetRawValue();
        auto itr = sAFKAuraStates.find(guid);
        if (itr == sAFKAuraStates.end())
            return;

        RemoveAppliedEffects(player, itr->second);
        sAFKAuraStates.erase(itr);
    }

    bool IsEligible(Player* player)
    {
        if (!player || !player->IsInWorld() || !player->IsAlive())
            return false;

        if (sAFKAuraConfig.ignoreGameMasters && player->IsGameMaster())
            return false;

        if (sAFKAuraConfig.ignoreInCombat && player->IsInCombat())
            return false;

        if (sAFKAuraConfig.ignoreBattlegrounds && player->InBattleground())
            return false;

        if (sAFKAuraConfig.ignoreFlight && player->IsInFlight())
            return false;

        return true;
    }

    void LoadAFKAuraConfig()
    {
        sAFKAuraConfig.enabled = sConfigMgr->GetOption<bool>("AFKAura.Enable", false);
        sAFKAuraConfig.auraSpellId = sConfigMgr->GetOption<uint32>("AFKAura.AuraSpellId", 0);
        sAFKAuraConfig.idleTimeMs = sConfigMgr->GetOption<uint32>("AFKAura.IdleTimeSeconds", 300) * 1000;
        sAFKAuraConfig.ignoreGameMasters = sConfigMgr->GetOption<bool>("AFKAura.IgnoreGameMasters", true);
        sAFKAuraConfig.ignoreInCombat = sConfigMgr->GetOption<bool>("AFKAura.IgnoreInCombat", false);
        sAFKAuraConfig.ignoreBattlegrounds = sConfigMgr->GetOption<bool>("AFKAura.IgnoreBattlegrounds", true);
        sAFKAuraConfig.ignoreFlight = sConfigMgr->GetOption<bool>("AFKAura.IgnoreFlight", true);
        sAFKAuraConfig.setPlayerFlagAFK = sConfigMgr->GetOption<bool>("AFKAura.SetPlayerFlagAFK", false);

        if (sAFKAuraConfig.enabled && sAFKAuraConfig.auraSpellId && !sSpellMgr->GetSpellInfo(sAFKAuraConfig.auraSpellId))
        {
            LOG_ERROR("server.loading", "AFKAura.AuraSpellId ({}) does not exist. Module will stay enabled but won't apply aura until a valid spell id is configured.", sAFKAuraConfig.auraSpellId);
        }
    }

    class AFKAuraWorldScript : public WorldScript
    {
    public:
        AFKAuraWorldScript() : WorldScript("AFKAuraWorldScript") { }

        void OnAfterConfigLoad(bool /*reload*/) override
        {
            LoadAFKAuraConfig();
        }
    };

    class AFKAuraPlayerScript : public PlayerScript
    {
    public:
        AFKAuraPlayerScript() : PlayerScript("AFKAuraPlayerScript",
        {
            PLAYERHOOK_ON_LOGIN,
            PLAYERHOOK_ON_LOGOUT,
            PLAYERHOOK_ON_UPDATE,
            PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
            PLAYERHOOK_ON_EMOTE,
            PLAYERHOOK_ON_TEXT_EMOTE,
            PLAYERHOOK_ON_SPELL_CAST
        }) { }

        void OnPlayerLogin(Player* player) override
        {
            ResetActivity(player);
        }

        void OnPlayerLogout(Player* player) override
        {
            ClearState(player);
        }

        void OnPlayerUpdate(Player* player, uint32 diff) override
        {
            if (!player)
                return;

            AFKAuraState& state = GetState(player);

            if (!sAFKAuraConfig.enabled || !sAFKAuraConfig.auraSpellId || !IsEligible(player))
            {
                state.idleMs = 0;
                RemoveAppliedEffects(player, state);
                return;
            }

            if (state.idleMs < sAFKAuraConfig.idleTimeMs)
            {
                uint32 const remaining = sAFKAuraConfig.idleTimeMs - state.idleMs;
                state.idleMs += diff > remaining ? remaining : diff;
            }

            if (state.idleMs < sAFKAuraConfig.idleTimeMs)
                return;

            if (!state.auraApplied)
            {
                if (player->AddAura(sAFKAuraConfig.auraSpellId, player))
                    state.auraApplied = true;
            }

            if (sAFKAuraConfig.setPlayerFlagAFK && !player->HasPlayerFlag(PLAYER_FLAGS_AFK))
            {
                player->SetPlayerFlag(PLAYER_FLAGS_AFK);
                state.afkFlagApplied = true;
            }
        }

        void OnPlayerBeforeSendChatMessage(Player* player, uint32& /*type*/, uint32& /*lang*/, std::string& /*msg*/) override
        {
            ResetActivity(player);
        }

        void OnPlayerEmote(Player* player, uint32 /*emote*/) override
        {
            ResetActivity(player);
        }

        void OnPlayerTextEmote(Player* player, uint32 /*textEmote*/, uint32 /*emoteNum*/, ObjectGuid /*guid*/) override
        {
            ResetActivity(player);
        }

        void OnPlayerSpellCast(Player* player, Spell* /*spell*/, bool /*skipCheck*/) override
        {
            ResetActivity(player);
        }
    };

    class AFKAuraMovementScript : public MovementHandlerScript
    {
    public:
        AFKAuraMovementScript() : MovementHandlerScript("AFKAuraMovementScript", { MOVEMENTHOOK_ON_PLAYER_MOVE }) { }

        void OnPlayerMove(Player* player, MovementInfo /*movementInfo*/, uint32 /*opcode*/) override
        {
            ResetActivity(player);
        }
    };
}

void AddAFKAuraScripts()
{
    new AFKAuraWorldScript();
    new AFKAuraPlayerScript();
    new AFKAuraMovementScript();
}
