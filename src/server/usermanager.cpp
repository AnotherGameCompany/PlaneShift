/*
* usermanager.cpp - Author: Keith Fulton
*
* Copyright (C) 2001 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
*
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation (version 2 of the License)
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/
#include <psconfig.h>
#include <ctype.h>
#include <string.h>
#include <memory.h>
//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/randomgen.h>
#include <csutil/sysfunc.h>
#include <iengine/mesh.h>
#include <iengine/movable.h>
#include <iengine/sector.h>
#include <iutil/object.h>
#include <iengine/region.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "util/psdatabase.h"
#include "util/serverconsole.h"
#include "util/pserror.h"
#include "util/psconst.h"
#include "util/log.h"
#include "util/eventmanager.h"

#include "engine/celbase.h"
#include "engine/netpersist.h"
#include "engine/psworld.h"

#include "bulkobjects/pscharacter.h"
#include "bulkobjects/psraceinfo.h"
#include "bulkobjects/psguildinfo.h"
#include "bulkobjects/pscharacterloader.h"
#include "bulkobjects/psactionlocationinfo.h"

#include "rpgrules/factions.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "usermanager.h"
#include "client.h"
#include "clients.h"
#include "events.h"
#include "gem.h"
#include "netmanager.h"
#include "entitymanager.h"
#include "marriagemanager.h"
#include "combatmanager.h"
#include "invitemanager.h"
#include "adminmanager.h"
#include "commandmanager.h"
#include "psserver.h"
#include "cachemanager.h"
#include "playergroup.h"
#include "progressionmanager.h"
#include "netmanager.h"
#include "advicemanager.h"
#include "actionmanager.h"
#include "introductionmanager.h"
#include "chatmanager.h"
#include "gmeventmanager.h"
#include "bankmanager.h"
#include "globals.h"

#define RANGE_TO_CHALLENGE 50


class psUserStatRegeneration : public psGameEvent
{
protected:
    UserManager * usermanager;

public:
    psUserStatRegeneration(UserManager *mgr,csTicks ticks);

    virtual void Trigger();  // Abstract event processing function
};

/** A structure to hold the clients that are pending on duel challenges.
*/
class PendingDuelInvite : public PendingInvite
{
public:

    PendingDuelInvite(Client *inviter,
        Client *invitee,
        const char *question)
        : PendingInvite( inviter, invitee, true,
        question,"Accept","Decline",
        "You have challenged %s to a duel.",
        "%s has challenged you to a duel.",
        "%s has accepted your challenge.",
        "You have accepted %s's challenge.",
        "%s has declined your challenge.",
        "You have declined %s's challenge.", psQuestionMessage::duelConfirm)
    {
    }

    virtual ~PendingDuelInvite() {}

    void HandleAnswer(const csString & answer);
};

/***********************************************************************/

UserManager::UserManager(ClientConnectionSet *cs)
{
    clients       = cs;

    psserver->GetEventManager()->Subscribe(this,MSGTYPE_USERCMD,REQUIRE_READY_CLIENT|REQUIRE_ALIVE);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_MOTDREQUEST,REQUIRE_ANY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_CHARDETAILSREQUEST,REQUIRE_READY_CLIENT);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_CHARDESCUPDATE,REQUIRE_READY_CLIENT);

    psserver->GetEventManager()->Subscribe(this,MSGTYPE_TARGET_EVENT,NO_VALIDATION);
    psserver->GetEventManager()->Subscribe(this,MSGTYPE_ENTRANCE,REQUIRE_READY_CLIENT);
}

UserManager::~UserManager()
{
    if (psserver->GetEventManager())
    {
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_USERCMD);
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_MOTDREQUEST);
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_CHARDETAILSREQUEST);
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_CHARDESCUPDATE);

        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_TARGET_EVENT);
        psserver->GetEventManager()->Unsubscribe(this,MSGTYPE_ENTRANCE);
    }
}

void UserManager::HandleMOTDRequest(MsgEntry *me,Client *client)
{
    //Sends MOTD and tip

    unsigned int guildID =0;
    //If data isn't loaded, load from db
    if (!client->GetCharacterData())
    {
        Result result(db->Select("SELECT guild_member_of FROM characters WHERE id = '%d'",client->GetPlayerID()));
        if (result.Count() > 0)
            guildID = result[0].GetUInt32(0);
    }
    else
    {
        psGuildInfo* playerGuild = client->GetCharacterData()->GetGuild();

        if (playerGuild)
            guildID = playerGuild->id;
        else
            guildID =0;
    }

    csString tip;
    if (CacheManager::GetSingleton().GetTipLength() > 0)
        CacheManager::GetSingleton().GetTipByID(psserver->GetRandom(CacheManager::GetSingleton().GetTipLength()), tip );

    csString motdMsg(psserver->GetMOTD());

    csString guildMotd("");
    csString guildName("");
    psGuildInfo * guild = CacheManager::GetSingleton().FindGuild(guildID);

    if (guild)
    {
        guildMotd=guild->GetMOTD();
        guildName=guild->GetName();
    }

    psMOTDMessage motd(me->clientnum,tip,motdMsg,guildMotd,guildName);
    motd.SendMessage();
    return;
}

void UserManager::HandleUserCommand(MsgEntry *me,Client *client)
{
    if (client->IsFrozen()) //disable most commands
        return;

    psUserCmdMessage msg(me);

    Debug3(LOG_USER, client->GetClientNum(),"Received user command: %s from %s\n",
        me->bytes->payload, (const char *)client->GetName());

    // We don't check for validity for emotes, they always are.
    if (!msg.valid && !CheckForEmote(msg.command, false, me, client))
    {
        psserver->SendSystemError(me->clientnum,"Command not supported by server yet.");
        return;
    }

    if (msg.command == "/who")
    {
        Who(msg,client,me->clientnum);
    }
    else if (msg.command == "/buddy")
    {
        Buddy(msg,client,me->clientnum);
    }
    else if (msg.command == "/notbuddy")
    {
        NotBuddy(msg,client, me->clientnum);
    }
    else if (msg.command == "/buddylist")
    {
        BuddyList(client,me->clientnum, UserManager::ALL_PLAYERS);
    }
    else if (msg.command == "/roll")
    {
        RollDice(msg,client,me->clientnum);
    }
    else if (msg.command == "/pos")
    {
        ReportPosition(msg,client,me->clientnum);
    }
    /*else if (msg.command == "/spawn")
    {
    MoveToSpawnPos(msg,client,me->clientnum);*
    }*/
    else if (msg.command == "/unstick")
    {
        HandleUnstick(msg,client,me->clientnum);
    }
    else if (msg.command == "/attack")
    {
        HandleAttack(msg,client,me->clientnum);
    }
    else if (msg.command == "/stopattack")
    {
        psserver->combatmanager->StopAttack(client->GetActor());
    }
    else if ( msg.command == "/admin" )
    {
        psserver->GetAdminManager()->Admin(client->GetPlayerID(), me->clientnum, client);
    }
    else if ( msg.command == "/loot" )
    {
        HandleLoot(client);
    }
    else if ( msg.command == "/quests" )
    {
        HandleQuests(client);
        HandleGMEvents(client);
    }
    else if ( msg.command == "/train" )
    {
        HandleTraining(client);
    }
    else if ( msg.command == "/sit" )
    {
        if (client->GetActor()->GetMode() == PSCHARACTER_MODE_PEACE && client->GetActor()->AtRest() && !client->GetActor()->IsFalling())
        {
            client->GetActor()->SetMode(PSCHARACTER_MODE_SIT);
            Emote("%s takes a seat.", "%s takes a seat by %s.", "sit", me, client);
        }
    }
    else if ( msg.command == "/stand" )
    {
        if (client->GetActor()->GetMode() == PSCHARACTER_MODE_SIT)
        {
            client->GetActor()->SetMode(PSCHARACTER_MODE_PEACE);
            psUserActionMessage anim(me->clientnum, client->GetActor()->GetEntityID(), "stand up");
            anim.Multicast( client->GetActor()->GetMulticastClients(),0,PROX_LIST_ANY_RANGE );
            Emote("%s stands up.", "%s stands up.", "stand", me, client);
        }
    }
    else if (msg.command == "/starttrading")
    {
        client->GetCharacterData()->SetTradingStopped(false);
        psserver->SendSystemInfo(me->clientnum,"You can trade now.");
    }
    else if (msg.command == "/stoptrading")
    {
        client->GetCharacterData()->SetTradingStopped(true);
        psserver->SendSystemInfo(me->clientnum,"You are busy and can't trade.");
    }
    else if ( msg.command == "/assist" )
    {
        Assist( msg, client, me->clientnum );
    }

    /*
    else if (msg.command == "/advisormode")
    {
    Advisor(client, me->clientnum, msg);
    }
    */
    else if (msg.command == "/tip")
    {
        GiveTip(me->clientnum);
    }
    else if (msg.command == "/motd")
    {
        GiveMOTD(me->clientnum);
    }
    else if (msg.command == "/challenge")
    {
        ChallengeToDuel(msg,client);
    }
    else if (msg.command == "/yield")
    {
        YieldDuel(client);
    }
    else if ( msg.command == "/die" )
    {
        gemActor* actor = client->GetActor();
        if (!actor)
            return;
        actor->Kill(actor);
    }
    else if (msg.command == "/marriage")
    {
        if (msg.action == "propose")
        {
            if ( msg.player.IsEmpty() || msg.text.IsEmpty() )
            {
                psserver->SendSystemError( client->GetClientNum(), "Usage: /marriage propose [first name] [message]" );
                return;
            }

            // Send propose message
            psserver->GetMarriageManager()->Propose(client,  msg.player,  msg.text);
        }
        else if (msg.action == "divorce")
        {
            if ( msg.text.IsEmpty() )
            {
                psserver->SendSystemError( client->GetClientNum(), "Usage: /marriage divorce [message]" );
                return;
            }

            // Send divorce prompt
            psserver->GetMarriageManager()->ContemplateDivorce(client,  msg.text);
        }
        else 
        {
            psserver->SendSystemError( client->GetClientNum(), "Usage: /marriage [propose|divorce]" );
        }
    }
    else if (msg.command == "/listemotes")
    {
        psserver->SendSystemInfo(me->clientnum, "List of emotes:");
        for(unsigned int i=0; i < emoteList.GetSize(); i++)
        {
            psserver->SendSystemInfo(me->clientnum, emoteList[i].command);
        }
    }
    else if(CheckForEmote(msg.command, true, me, client))
    {
        return;
    }
    else if (msg.command == "/bank")
    {
        HandleBanking(client, msg.action);
    }
    else
    {
        psserver->SendSystemError(me->clientnum,"Command not supported by server yet.");
    }
}

bool UserManager::CheckForEmote(csString command, bool execute, MsgEntry *me, Client *client)
{
    for(unsigned int i=0;  i < emoteList.GetSize(); i++)
    {
        if( command == emoteList[i].command )
        {
            if(execute)
            {
                Emote(emoteList[i].general, emoteList[i].specific, emoteList[i].anim, me, client);
            }
            return true;
        }
    }
    return false;
}

void UserManager::Emote(csString general, csString specific, csString animation, MsgEntry *me, Client *client)
{
    if (client->GetTargetObject() && (client->GetTargetObject() != client->GetActor()))
    {
        psSystemMessage newmsg(me->clientnum, MSG_INFO_BASE, specific, client->GetActor()->GetName(), client->GetTargetObject()->GetName());
        newmsg.Multicast(client->GetActor()->GetMulticastClients(), 0, CHAT_SAY_RANGE);
    }
    else
    {
        psSystemMessage newmsg(me->clientnum, MSG_INFO_BASE, general, client->GetActor()->GetName());
        newmsg.Multicast(client->GetActor()->GetMulticastClients(), 0, CHAT_SAY_RANGE);
    }

    if (animation != "noanim")
    {
        psUserActionMessage anim(me->clientnum, client->GetActor()->GetEntityID(), animation);
        anim.Multicast(client->GetActor()->GetMulticastClients(), 0, PROX_LIST_ANY_RANGE);
    }
}

bool UserManager::LoadEmotes(const char *xmlfile, iVFS *vfs)
{
    csRef<iDocumentSystem> xml = csPtr<iDocumentSystem>(new csTinyDocumentSystem);

    csRef<iDataBuffer> buff = vfs->ReadFile( xmlfile );

    if ( !buff || !buff->GetSize() )
    {
        return false;
    }

    csRef<iDocument> doc = xml->CreateDocument();
    const char* error = doc->Parse( buff );
    if ( error )
    {
        Error3("%s in %s", error, xmlfile);
        return false;
    }
    csRef<iDocumentNode> root    = doc->GetRoot();
    if(!root)
    {
        Error2("No XML root in %s", xmlfile);
        return false;
    }

    csRef<iDocumentNode> topNode = root->GetNode("emotes");
    if(!topNode)
    {
        Error2("No <emotes> tag in %s", xmlfile);
        return false;
    }
    csRef<iDocumentNodeIterator> emoteIter = topNode->GetNodes("emote");

    while(emoteIter->HasNext())
    {
        csRef<iDocumentNode> emoteNode = emoteIter->Next();

        EMOTE emote;
        emote.command = emoteNode->GetAttributeValue("command");
        emote.general = emoteNode->GetAttributeValue("general");
        emote.specific = emoteNode->GetAttributeValue("specific");
        emote.anim = emoteNode->GetAttributeValue("anim");

        emoteList.Push(emote);
    }

    return true;
}

void UserManager::HandleCharDetailsRequest(MsgEntry *me,Client *client)
{
    psCharacterDetailsRequestMessage msg(me);

    gemActor *myactor;
    if (!msg.isMe)
    {
        gemObject *target = client->GetTargetObject();
        if (!target)    return;

        myactor = target->GetActorPtr();
        if (!myactor) return;
    }
    else
    {
        myactor = client->GetActor();
        if (!myactor) return;
    }


    psCharacter* charData = myactor->GetCharacterData();
    if (!charData) return;


    SendCharacterDescription(client, charData, false, msg.isSimple, msg.requestor);
}

csString intToStr(int f)
{
    csString s;
    s.Format("%i", f);
    return s;
}

csString fmtStatLine(const char *const label, unsigned int value, unsigned int buffed)
{
    csString s;
    if (value != buffed)
        s.Format("%s: %u (%u)\n", label, value, buffed);
    else
        s.Format("%s: %u\n", label, value);
    return s;
}

void UserManager::SendCharacterDescription(Client * client, psCharacter * charData, bool full, bool simple, const csString & requestor)
{
    StatSet* playerAttr = client->GetCharacterData()->GetAttributes();

    bool isSelf = (charData->GetCharacterID() == client->GetCharacterData()->GetCharacterID());

    csString charName = charData->GetCharFullName();
    csString raceName = charData->GetRaceInfo()->name;
    csString desc     = charData->GetDescription();
    csArray<psCharacterDetailsMessage::NetworkDetailSkill> skills;

    if ( !simple && CacheManager::GetSingleton().GetCommandManager()->Validate(client->GetSecurityLevel(), "view stats") )
        full = true;  // GMs can view the stats list

    if (full)
    {
        desc += "\n\n";
        desc += "HP: "+intToStr(int(charData->GetHP()))+" Max HP: "+intToStr(int(charData->GetHitPointsMax()))+"\n";
        SkillSet *sks = charData->GetSkills();

        for(int skill = 0; sks && skill < PSSKILL_COUNT; skill++)
        {
            psSkillInfo *skinfo;
            skinfo = CacheManager::GetSingleton().GetSkillByID((PSSKILL)skill);
            if (skinfo != NULL) {
                psCharacterDetailsMessage::NetworkDetailSkill s;

                s.category = skinfo->category;
                s.text = fmtStatLine( skinfo->name,
                    sks->GetSkillRank((PSSKILL)skill, false),
                    sks->GetSkillRank((PSSKILL)skill, true));
                skills.Push(s);
            }
        }

        csHash<FactionStanding*, int>::GlobalIterator iter(
            charData->GetActor()->GetFactions()->GetStandings().GetIterator());
        while(iter.HasNext())
        {
            FactionStanding* standing = iter.Next();
            psCharacterDetailsMessage::NetworkDetailSkill s;

            s.category = 5; // faction
            s.text.Format("%s: %d\n", standing->faction->name.GetDataSafe(),
                standing->score);
            skills.Push(s);
        }
    }

    // No fancy things if simple description is requested
    if ( !simple )
    {
        // Don't guess strength if we can't attack the character or if he's
        //  dead or if we are viewing our own description
        if ( !charData->impervious_to_attack && (charData->GetMode() != PSCHARACTER_MODE_DEAD) && !isSelf )
        {
            if ( playerAttr->GetStat(PSITEMSTATS_STAT_INTELLIGENCE) < 50 )
                desc.AppendFmt( "\n\nYou try to evaluate the strength of %s, but you have no clue.", charName.GetData() );
            else
            {
                // Character's Strength assessment code below.
                static const char* const StrengthGuessPhrases[] =
                { "won't require any effort to defeat",
                "is noticeably weaker than you",
                "won't pose much of a challenge",
                "is not quite as strong as you",
                "is about as strong as you",
                "is somewhat stronger than you",
                "will pose a challenge to defeat",
                "is significantly more powerful than you",
                "may be impossible to defeat" };

                bool smart = (playerAttr->GetStat(PSITEMSTATS_STAT_INTELLIGENCE) >= 100);

                int CharsLvl      = charData->GetCharLevel();
                int PlayersLvl    = client->GetCharacterData()->GetCharLevel();
                int LvlDifference = PlayersLvl - CharsLvl;
                int Phrase        = 0;

                if ( LvlDifference >= 50 )
                    Phrase = 0;
                else if ( LvlDifference > 30 )
                    Phrase = 1;
                else if ( LvlDifference > 15 && smart)
                    Phrase = 2;
                else if ( LvlDifference > 5 )
                    Phrase = 3;
                else if ( LvlDifference >= -5 )
                    Phrase = 4;
                else if ( LvlDifference >= -15 )
                    Phrase = 5;
                else if ( LvlDifference >= -30 && smart)
                    Phrase = 6;
                else if ( LvlDifference > -50 )
                    Phrase = 7;
                else
                    Phrase = 8;

                // Enable for Debugging only
                // desc+="\n CharsLvl: "; desc+=CharsLvl; desc+=" | YourLvl: "; desc+=PlayersLvl; desc+="\n";

                desc.AppendFmt( "\n\nYou evaluate that %s %s.", charName.GetData(), StrengthGuessPhrases[Phrase] );
            }
        }

        // Show spouse name if character is married
        if ( charData->GetIsMarried() && !isSelf )
            desc.AppendFmt( "\n\nMarried to: %s", charData->GetSpouseName() );


        // Show owner name if character is a pet
        if ( charData->IsPet() && charData->GetOwnerID() )
        {
            gemActor *owner = GEMSupervisor::GetSingleton().FindPlayerEntity( charData->GetOwnerID() );
            if (owner)
                desc.AppendFmt( "\n\nA pet owned by: %s", owner->GetName() );
        }
    }

    if (!(charData->IsNPC() || charData->IsPet()) && client->GetSecurityLevel() < GM_LEVEL_0 &&
        !psserver->GetIntroductionManager()->IsIntroduced(client->GetCharacterData()->GetCharacterID(),
                                                         charData->GetCharacterID()) && !isSelf)
    {
        charName = "[Unknown]";
    }

    // Finally send the details message
    psCharacterDetailsMessage detailmsg(client->GetClientNum(), charName, 
        (short unsigned int)charData->GetRaceInfo()->gender, raceName,
        desc, skills, requestor );
    detailmsg.SendMessage();
}

void UserManager::HandleCharDescUpdate(MsgEntry *me,Client *client)
{
    psCharacterDescriptionUpdateMessage descUpdate(me);
    psCharacter* charData = client->GetCharacterData();
    if (!charData)
        return;

    charData->SetDescription(descUpdate.newValue);
    Debug3(LOG_USER, client->GetClientNum(), "Character description updated for %s (%d)\n",charData->GetCharFullName(),client->GetAccountID());
}

void UserManager::HandleTargetEvent(MsgEntry *me)
{
    psTargetChangeEvent targetevent(me);

    Client *targeter = NULL;
    Client *targeted = NULL;
    if (targetevent.character)
    {
        Debug2(LOG_USER, targetevent.character->GetClientID(),"UserManager handling target event for %s\n", targetevent.character->GetName());
        targeter = clients->Find( targetevent.character->GetClientID() );
        if (!targeter)
            return;
    }
    if (targetevent.target)
        targeted = clients->Find( targetevent.target->GetClientID() );

    psserver->combatmanager->StopAttack(targeter->GetActor());

    if(!targeted
        && dynamic_cast<gemNPC*>(targetevent.target)
        && targetevent.character->GetMode() == PSCHARACTER_MODE_COMBAT) // NPC?
    {
        if (targeter->IsAllowedToAttack(targetevent.target))
            SwitchAttackTarget( targeter, targeted );

        return;
    }
    else if (!targeted)
        return;

    if (targeted->IsReady() && targeter->IsReady() )
    {
        if (targetevent.character->GetMode() == PSCHARACTER_MODE_COMBAT)
            SwitchAttackTarget( targeter, targeted );
    }
    else
    {
        psserver->SendSystemError(targeter->GetClientNum(),"Target is not ready yet");
    }
}

void UserManager::HandleEntranceMessage( MsgEntry* me, Client *client )
{
    psEntranceMessage mesg(me);
    psActionLocation *action = psserver->GetActionManager()->FindAction( mesg.entranceID );
    if ( !action )
    {
        Error2( "No item/action : %d", mesg.entranceID );
        return;
    }

    // Check range 
    csWeakRef<gemObject> gem = client->GetActor();
    csWeakRef<gemObject> gemAction = action->GetGemObject();
    if (gem.IsValid() && gemAction.IsValid() && gem->RangeTo(gemAction, false) > RANGE_TO_SELECT)
    {
        psserver->SendSystemError(client->GetClientNum(), "You are no longer in range to do this.");
        return;
    }

    // Check for entrance
    if ( !action->IsEntrance() ) 
    {
        Error1("No <Entrance> tag in action response");
        return;
    }

    // Check for a lock
    uint32 instance_id = action->GetInstanceID();
    if ( instance_id  != 0 )
    {

        // find lock to to test if locked
        gemItem* realItem = GEMSupervisor::GetSingleton().FindItemEntity( instance_id );
        if (!realItem)
        {
            Error3("Invalid instance ID %u in action location %s", instance_id, action->name.GetDataSafe());
            return;
        }

        // get real item
        psItem* item = realItem->GetItem();
        if ( !item )
        {
            Error1("Invalid ItemID in Action Location Response.\n");
            return;
        }

        // Check if locked 
        if(item->GetIsLocked())
        {
            if (!client->GetCharacterData()->Inventory().HaveKeyForLock(item->GetUID()))
            {
                psserver->SendSystemInfo(client->GetClientNum(),"Entrance is locked.");
                return;
            }
        }
    }

    // Get entrance attributes
    csVector3 pos = action->GetEntrancePosition();
    float rot = action->GetEntranceRotation();
    csString sectorName = action->GetEntranceSector();

    // Check for different entrance types
    csString entranceType = action->GetEntranceType();

    // Send player to main game instance
    if (entranceType == "Prime")
    {
        Teleport( client, pos.x, pos.y, pos.z, 0, rot, sectorName );
    }

    // Send player to unique instance
    else if (entranceType == "ActionID")
    {
        int instance = (int)action->id;
        Teleport( client, pos.x, pos.y, pos.z, instance, rot, sectorName );
    }

    // Send player back to starting point
    else if (entranceType == "ExitActionID")
    {
        // Use current player instance to get entrance action location
        psActionLocation *retAction = psserver->GetActionManager()->FindActionByID( client->GetActor()->GetInstance() );
        if ( !retAction )
        {
            Error2( "No item/action : %i", client->GetActor()->GetInstance() );
            return;
        }

        // Check for return entrance
        if ( !retAction->IsReturn() ) 
        {
            Error2("No <Return tag in action response %s",retAction->response.GetData());
            return;
        }

        // Process return attributes and spin us around 180 deg for exit
        csVector3 retPos = retAction->GetReturnPosition();
        float retRot = retAction->GetReturnRotation() + (3.141592654/2);
        csString retSectorName = retAction->GetReturnSector();

        // Send player back to return point
        Teleport( client, retPos.x, retPos.y, retPos.z, 0, retRot, retSectorName );
    }
    else
    {
        Error1("Unknown type in entrance action location");
    }
}

void UserManager::HandleMessage(MsgEntry *me,Client *client)
{
    switch (me->GetType())
    {
    case MSGTYPE_MOTDREQUEST:
        {
            HandleMOTDRequest(me,client);
            break;
        }
    case MSGTYPE_USERCMD:
        {
            HandleUserCommand(me,client);
            break;
        }
    case MSGTYPE_CHARDETAILSREQUEST:
        {
            HandleCharDetailsRequest(me,client);
            break;
        }
    case MSGTYPE_CHARDESCUPDATE:
        {
            HandleCharDescUpdate(me,client);
            break;
        }
    case MSGTYPE_TARGET_EVENT:
        {
            HandleTargetEvent(me);
            break;
        }
    case MSGTYPE_ENTRANCE:
        {
            HandleEntranceMessage(me, client);
            break;
        }

    }
}

void UserManager::Who(psUserCmdMessage& msg, Client* client, int clientnum)
{
    csString message((size_t) 1024);
    csString temp((size_t) 1024);
    csString headerMsg("Players Currently Online");

    if (!msg.filter.IsEmpty())
    {
        size_t pos = msg.filter.FindFirst('%');
        while (pos != (size_t) -1)
        {
            msg.filter.DeleteAt(pos);
            pos = msg.filter.FindFirst('%');
        }

        StrToLowerCase(msg.filter);

        headerMsg.Append(" (Applying Filter: '*");
        headerMsg.Append(msg.filter);
        headerMsg.Append("*')");
    }

    message.Append(headerMsg);
    headerMsg.Append("\n-------------------------------------");

    unsigned count = 0;

    // Guild rank, guild and title should come from acraig's player prop class.
    ClientIterator i(*clients);     
    for (Client* curr = i.First(); curr && count<30; curr = i.Next()) 
    {  
        csString playerName(curr->GetName());
        csString guildTitle;
        csString guildName;
        csString format("%s"); // Player name.

        if (curr->IsSuperClient() || !curr->GetActor())
            continue;

        psGuildInfo* guild = curr->GetActor()->GetGuild();
        if (guild != NULL)
        {
            if (guild->id && (!guild->IsSecret()
                || guild->id == client->GetGuildID()))
            {
                psGuildLevel* level = curr->GetActor()->GetGuildLevel();
                if (level)
                {
                    format.Append(", %s in %s"); // Guild level title.
                    guildTitle = level->title;
                }
                else
                {
                    format.Append(", %s");
                }
                guildName = guild->name;
            }
        }
        temp.Format(format.GetData(), curr->GetName(), guildTitle.GetData(),
            guildName.GetData());

        csString lower(temp);
        StrToLowerCase(lower);
        if (!msg.filter.IsEmpty() && lower.FindStr(msg.filter) == (size_t)-1)
            continue;

        // If the message is too big, send in chunks.
        if (temp.Length() + message.Length() > 1000)
        {
            psSystemMessageSafe newmsg(clientnum ,MSG_WHO, message);
            newmsg.SendMessage();

            message.Clear();
        }
        else
            message.Append('\n');
        message.Append(temp);

        count++;
    }

    // Could be about to overflow by now, so check.
    temp.Format("%u shown from %zu players online\n", count, clients->Count());
    if (temp.Length() + message.Length() > 1000)
    {
        psSystemMessageSafe newmsg(clientnum ,MSG_WHO, message);
        newmsg.SendMessage();
        message.Clear();
    }
    else
        message.Append('\n');
    message.Append(temp);

    psSystemMessageSafe newmsg(clientnum ,MSG_WHO, message);
    newmsg.SendMessage();
}

void UserManager::StrToLowerCase(csString& str)
{
    for (register size_t i = 0; i < str.Length(); ++i)
        str[i] = tolower(str[i]);
}

void UserManager::Buddy(psUserCmdMessage& msg,Client *client,int clientnum)
{
    msg.player = NormalizeCharacterName(msg.player);

    if (msg.player.Length() == 0)
    {
        psserver->SendSystemError(clientnum,"The character name of your buddy must be specified.");
        return;
    }
    if (client->GetCharacterData()==NULL)
    {
        Error3("Client for account '%s' attempted to add buddy '%s' but has no character data!",client->GetName(),msg.player.GetData());
        return;
    }


    unsigned int selfid=client->GetCharacterData()->GetCharacterID();

    bool searchNPCs = false;
    unsigned int buddyid=psServer::CharacterLoader.FindCharacterID(msg.player.GetData(), searchNPCs);

    if (buddyid==0)
    {
        psserver->SendSystemError(clientnum,"Could not add buddy: Character '%s' not found.", msg.player.GetData());
        return;
    }

    if ( !client->GetCharacterData()->AddBuddy( buddyid, msg.player ) )
    {
        psserver->SendSystemError(clientnum,"%s could not be added to buddy list.",(const char *)msg.player);
        return;
    }

    Client* buddyClient = clients->FindPlayer( buddyid );
    if ( buddyClient && buddyClient->IsReady() )
    {
        buddyClient->GetCharacterData()->BuddyOf( selfid );
    }



    if (!psserver->AddBuddy(selfid,buddyid))
    {
        psserver->SendSystemError(clientnum,"%s is already on your buddy list.",(const char *)msg.player);
        return;
    }

    BuddyList( client, clientnum, true );

    psserver->SendSystemInfo(clientnum,"%s has been added to your buddy list.",(const char *)msg.player);
}


void UserManager::NotBuddy(psUserCmdMessage& msg,Client *client,int clientnum)
{
    msg.player = NormalizeCharacterName(msg.player);

    if (msg.player.Length() == 0)
    {
        psserver->SendSystemError(clientnum,"The character name of your buddy must be specified.");
        return;
    }
    if (client->GetCharacterData()==NULL)
    {
        Error3("Client for account '%s' attempted to remove buddy '%s' but has no character data!",client->GetName(),msg.player.GetData());
        return;
    }
    unsigned int selfid=client->GetCharacterData()->GetCharacterID();

    bool searchNPCs = false;
    unsigned int buddyid=psServer::CharacterLoader.FindCharacterID(msg.player.GetData(), searchNPCs);
    if (buddyid==0)
    {
        psserver->SendSystemError(clientnum,"Could not remove buddy: Character '%s' not found.", msg.player.GetData());
        return;
    }

    client->GetCharacterData()->RemoveBuddy( buddyid );
    Client* buddyClient = clients->FindPlayer( buddyid );
    if ( buddyClient )
    {
        psCharacter* buddyChar = buddyClient->GetCharacterData();
        if (buddyChar)
            buddyChar->NotBuddyOf( selfid );
    }

    if (!psserver->RemoveBuddy(selfid,buddyid))
    {
        psserver->SendSystemError(clientnum,"%s is not on your buddy list.",(const char *)msg.player);
        return;
    }
    BuddyList( client, clientnum, true );

    psserver->SendSystemInfo(clientnum,"%s has been removed from your buddy list.",(const char *)msg.player);
}

void UserManager::BuddyList(Client *client,int clientnum,bool filter)
{
    psCharacter *chardata=client->GetCharacterData();
    if (chardata==NULL)
    {
        Error2("Client for account '%s' attempted to display buddy list but has no character data!",client->GetName());
        return;
    }

    int totalBuddies = (int)chardata->buddyList.GetSize(); //psBuddyListMsg should have as parameter a size_t. This is temporary.


    psBuddyListMsg mesg( clientnum, totalBuddies );
    for ( int i = 0; i < totalBuddies; i++ )
    {
        mesg.AddBuddy( i, chardata->buddyList[i].name, (clients->Find(chardata->buddyList[i].name)? true : false) );
    }

    mesg.Build();
    mesg.SendMessage();
}


void UserManager::NotifyBuddies(Client * client, bool logged_in)
{
    csString name (client->GetName());

    for (size_t i=0; i< client->GetCharacterData()->buddyOfList.GetSize(); i++)
    {
        Client *buddy = clients->FindPlayer( client->GetCharacterData()->buddyOfList[i] );  // name of player buddy

        if (buddy)  // is buddy online at the moment?  if so let him know buddy just logged on
        {
            psBuddyStatus status( buddy->GetClientNum(),  name , logged_in );
            status.SendMessage();

            if (logged_in)
            {
                psserver->SendSystemInfo(buddy->GetClientNum(),"%s just joined PlaneShift",client->GetName());
            }
            else
            {
                psserver->SendSystemInfo(buddy->GetClientNum(),"%s has quit",client->GetName());
            }
        }
    }
}

void UserManager::RollDice(psUserCmdMessage& msg,Client *client,int clientnum)
{
    int total=0;

    if (msg.dice > 10)
        msg.dice = 10;
    if (msg.sides > 10000)
        msg.sides = 10000;

    if ( msg.dice < 1 )
        msg.dice = 1;
    if ( msg.sides < 1 )
        msg.sides = 1;

    for (int i = 0; i<msg.dice; i++)
    {
        // must use msg.sides instead of msg.sides-1 because rand never actually
        // returns max val, and int truncation never results in max val as a result
        total = total + psserver->rng->Get(msg.sides) + 1;
    }

    if (msg.dice > 1)
    {
        psSystemMessage newmsg(clientnum,MSG_INFO_BASE,
            "Player %s has rolled %d %d-sided dice for a %d.",
            client->GetName(),
            msg.dice,
            msg.sides,
            total);

        newmsg.Multicast(client->GetActor()->GetMulticastClients(),0, 10);
    }
    else
    {
        psSystemMessage newmsg(clientnum,MSG_INFO_BASE,
            "Player %s has rolled a %d-sided die for a %d.",
            client->GetName(),
            msg.sides,
            total);

        newmsg.Multicast(client->GetActor()->GetMulticastClients(),0, 10);
    }
}


void UserManager::ReportPosition(psUserCmdMessage& msg,Client *client,int clientnum)
{
    gemObject *object = NULL;
    bool self = true;

    bool extras = CacheManager::GetSingleton().GetCommandManager()->Validate(client->GetSecurityLevel(), "pos extras");

    // Allow GMs to get other players' and entities' locations
    if (extras && msg.player.Length())
    {
        self = false;

        if (msg.player == "target")
        {
            object = client->GetTargetObject();
            if (!object)
            {
                psserver->SendSystemError(client->GetClientNum(), "You must have a target selected.");
                return;
            }
        }
        else
        {
            Client* c = psserver->GetAdminManager()->FindPlayerClient(msg.player);
            if (c) object = (gemObject*)c->GetActor();
            if (!object) object = psserver->GetAdminManager()->FindObjectByString(msg.player,client->GetActor());
        }
    }
    else
        object = client->GetActor();

    if (object)
    {
        csVector3 pos;
        iSector* sector = 0;
        float angle;

        object->GetPosition(pos, angle, sector);
        int instance = object->GetInstance();

        csString sector_name = (sector) ? sector->QueryObject()->GetName() : "(null)";

        csString name,range;
        if (self){
            name = "Your";
            range.Clear();
        }
        else
        {
            float dist = 0.0;
            csVector3 char_pos;
            float char_angle;
            iSector* char_sector;
            client->GetActor()->GetPosition(char_pos,char_angle,char_sector);
            dist = EntityManager::GetSingleton().GetWorld()->Distance(char_pos,char_sector,pos,sector);
            name.Format("%s's",object->GetName());
            range.Format(", range: %.2f",dist);
        }

        // Report extra info to GMs (players will use skills to determine correct direction)
        if (extras)
        {
            // Get the iRegion this sector belongs to
            csRef<iRegion> region =  scfQueryInterface<iRegion> (sector->QueryObject()->GetObjectParent());
            csString region_name = (region) ? region->QueryObject()->GetName() : "(null)";

            int degrees = (int)(angle*180.0/PI);
            psserver->SendSystemInfo(clientnum,
                "%s current position is %1.2f,%1.2f,%1.2f angle: %d in sector: %s, region: %s%s instance: %d",
                name.GetData(), pos.x, pos.y, pos.z, degrees,
                sector_name.GetData(), region_name.GetData(), range.GetData(), instance);
        }
        else
        {
            psserver->SendSystemInfo(clientnum,"%s current position is %1.2f,%1.2f,%1.2f in sector: %s, instance: %d",
                name.GetData(), pos.x, pos.y, pos.z, sector_name.GetData(), instance);
        }
    }
}

void UserManager::HandleUnstick(psUserCmdMessage& msg, Client *client,
                                int clientnum)
{
    gemActor *actor = client->GetActor();
    if (!actor)
        return;

    StopAllCombat(client);
    LogStuck(client);

    if (actor->MoveToValidPos())
    {
        csVector3 pos;
        iSector* sector;

        actor->GetPosition(pos, sector);
        psserver->SendSystemInfo(clientnum,
            "Moving back to valid position in sector %s...",
            sector->QueryObject()->GetName());
    }
    else
    {
        int timeRemaining = (UNSTICK_TIME - int(csGetTicks() - actor->GetFallStartTime())) / 1000;
        if (actor->IsFalling() && timeRemaining > 0)
            psserver->SendSystemError(clientnum, "You cannot /unstick yet - please wait %d %s and try again.", timeRemaining, timeRemaining == 1 ? "second" : "seconds");
        else
            psserver->SendSystemError(clientnum, "You cannot /unstick at this time.");
    }
}

void UserManager::LogStuck(Client* client)
{
    csVector3 pos;
    float yrot;
    iSector* sector;

    if (!client || !client->GetActor())
        return;

    client->GetActor()->GetPosition(pos, yrot, sector);
    psRaceInfo* race = client->GetActor()->GetCharacterData()->GetRaceInfo();

    csString buffer;
    buffer.Format("%s, %s, %d, %s, %.3f, %.3f, %.3f, %.3f", client->GetName(),
        race->name.GetDataSafe(), race->gender,
        sector->QueryObject()->GetName(), pos.x, pos.y, pos.z, yrot);
    psserver->GetLogCSV()->Write(CSV_STUCK, buffer);
}

void UserManager::StopAllCombat(Client *client)
{
    if (!client || !client->GetActor())
        return;

    if (client->GetDuelClientCount() > 0)
        YieldDuel(client);
    else
        psserver->combatmanager->StopAttack(client->GetActor());
}

void UserManager::HandleAttack(psUserCmdMessage& msg,Client *client,int clientnum)
{
    Attack(client->GetCharacterData()->getStance(msg.stance), client, clientnum);
}

void UserManager::Attack(Stance stance, Client *client,int clientnum)
{
    if (!client->IsAlive() || client->IsFrozen())
    {
        psserver->SendSystemError(client->GetClientNum(),"You are dead, you cannot fight now.");
        return;
    }

    gemObject *target = client->GetTargetObject();
    if ( ! target )
    {
        psserver->SendSystemError(clientnum,"You do not have a target selected.");
        return;
    }
    if (target->GetItem() || strcmp(target->GetObjectType(), "ActionLocation") == 0 )
    {
        psserver->SendSystemError(clientnum,"You cannot attack %s.", (const char*)target->GetName() );
        return;
    }
    if ( target->IsAlive() == false )
    {
        psserver->SendSystemError(clientnum,"%s is already dead.", (const char*)target->GetName() );
        return;
    }

    if (client->IsAllowedToAttack(target))
    {
        psserver->combatmanager->AttackSomeone(client->GetActor(), target, stance );
    }
}

void UserManager::Assist( psUserCmdMessage& msg, Client* client, int clientnum )
{
    Client* targetClient = NULL;

    if (!client->IsAlive())
    {
        psserver->SendSystemError(client->GetClientNum(),"You are dead, you cannot assist anybody now.");
        return;
    }

    // If the player doesn't provide an argument, use the players current
    // target instead.
    if ( msg.player.IsEmpty() )
    {
        int currentTarget = client->GetTargetClientID();
        if ( currentTarget == -1 )
        {
            psserver->SendSystemInfo( clientnum, "You have no target selected.");
            return;
        }

        if ( currentTarget == 0 )
        {
            psserver->SendSystemInfo( clientnum, "You can assist other players only.");
            return;
        }

        targetClient = clients->Find( currentTarget );
        if ( targetClient == NULL )
        {
            psserver->SendSystemInfo( clientnum, "Internal error - client not found.");
            return;
        }
    }
    else
    {
        csString playerName = NormalizeCharacterName(msg.player);
        targetClient = clients->Find( playerName );

        if ( !targetClient )
        {
            psserver->SendSystemInfo( clientnum,"Specified player is not online." );
            return;
        }
    }

    if ( targetClient == client )
    {
        psserver->SendSystemInfo( clientnum,"You cannot assist yourself." );
        return;
    }

    if ( !client->GetActor()->IsNear( targetClient->GetActor(), ASSIST_MAX_DIST ) )
    {
        psserver->SendSystemInfo(clientnum,
            "Specified player is too far away." );
        return;
    }

    gemObject* targetObject = targetClient->GetTargetObject();
    if(!targetObject)
    {
        psserver->SendSystemInfo(clientnum,
            "Specified player has no target selected." );
        return;
    }

    client->SetTargetObject( targetObject, true );
}

void UserManager::UserStatRegeneration()
{
    GEMSupervisor::GetSingleton().UpdateAllStats();

    // Push a new event
    psUserStatRegeneration* event;
    nextUserStatRegeneration += 1000;
    event = new psUserStatRegeneration(this,nextUserStatRegeneration);
    psserver->GetEventManager()->Push(event);
}

void UserManager::Ready()
{
    nextUserStatRegeneration = csGetTicks();
    UserStatRegeneration();
}

/**
* Check target dead
* Check target lootable by this client
* Return lootable items list if present
*/
void UserManager::HandleLoot(Client *client)
{
    int clientnum = client->GetClientNum();

    if (!client->IsAlive())
    {
        psserver->SendSystemError(client->GetClientNum(),"You are dead, you cannot loot now");
        return;
    }

    gemObject *target = client->GetTargetObject();
    if (!target)
    {
        psserver->SendSystemError(clientnum,"You don't have a target selected.");
        return;
    }

    if (target->IsAlive())
    {
        psserver->SendSystemError(clientnum, "You can't loot person that is alive");
        return;
    }

    // Check target lootable by this client
    gemNPC *npc = target->GetNPCPtr();
    if (!npc)
    {
        psserver->SendSystemError(clientnum, "You can loot NPCs only");
        return;
    }

    if (!npc->IsLootableClient(client->GetClientNum()))
    {
        Debug2(LOG_USER, client->GetClientNum(),"Client %d tried to loot mob that wasn't theirs.\n",client->GetClientNum() );
        psserver->SendSystemError(client->GetClientNum(),"You are not allowed to loot %s.",target->GetName() );
        return;
    }

    // Check to make sure loot is in range.
    if (client->GetActor()->RangeTo(target) > RANGE_TO_LOOT)
    {
        psserver->SendSystemError(client->GetClientNum(), "Too far away to loot %s", target->GetName() );         return;
    }


    // Return lootable items list if present
    psCharacter *chr = target->GetCharacterData();
    if (chr)
    {
        // Send items to looting player
        psLootMessage loot;
        size_t count = chr->GetLootItems(loot,
            target->GetEntityID(),
            client->GetClientNum() );
        if (count)
        {
            Debug3(LOG_LOOT, client->GetClientNum(), "Sending %zu loot items to %s.\n", count, client->GetActor()->GetName());
            loot.SendMessage();
        }
        else if(!count && !chr->GetLootMoney())
        {
            Debug1(LOG_LOOT, client->GetClientNum(),"Mob doesn't have loot.\n");
            psserver->SendSystemError(client->GetClientNum(),"%s has nothing to be looted.",target->GetName() );
        }

        // Split up money among LootableClients in group. 
        // These are those clients who were close enough when the NPC was killed.

        int money = chr->GetLootMoney();
        if (money)
        {
            Debug2(LOG_LOOT, client->GetClientNum(),"Splitting up %d money.\n", money);
            csRef<PlayerGroup> group = client->GetActor()->GetGroup();
            int remainder,each;
            if (group)
            {
                // Locate the group members who are in range to loot the trias
                csArray<gemActor*> closegroupmembers;
                size_t membercount = group->GetMemberCount();
                for (size_t i = 0 ; i < membercount ; i++)
                {
                    gemActor* currmember = group->GetMember(i);
                    if (!currmember)
                    {
                        continue;
                    }
                    //Copy all lootable clients.
                    //TODO: a direct interface to gemNPC::lootable_clients would be better
                    if (target->IsLootableClient(currmember->GetClientID()))
                    {
                        closegroupmembers.Push(currmember);
                    }
                }

                remainder = money % (int) closegroupmembers.GetSize();
                each      = money / (int) closegroupmembers.GetSize();
                psMoney eachmoney = psMoney(each).Normalized();
                psMoney remmoney  = psMoney(remainder).Normalized();
                csString remstr   = remmoney.ToUserString();
                csString eachstr  = eachmoney.ToUserString();

                if (each)
                {
                    psSystemMessage loot(client->GetClientNum(),MSG_LOOT,"Everyone nearby has looted %s.",eachstr.GetData());
                    client->GetActor()->SendGroupMessage(loot.msg);

                    // Send a personal message to each group member about the money
                    for (size_t i = 0 ; i < membercount ; i++)
                    {
                        gemActor* currmember = group->GetMember(i);
                        if (closegroupmembers.Contains(currmember) != csArrayItemNotFound)
                        {
                            //Normal loot is not shown in yellow. Until that happens, do not do it for few coins
                            //psserver->SendSystemResult(currmember->GetClient()->GetClientNum(), "You have looted %s.", eachstr.GetData());
                            psserver->SendSystemInfo(currmember->GetClient()->GetClientNum(), "You have looted %s.", eachstr.GetData());
                        }
                        else
                        {
                            // Something less intrusive for players who were too far away
                            psserver->SendSystemInfo(currmember->GetClient()->GetClientNum(), "You were too far away to loot.");
                        }
                    }

                    npc->AdjustMoneyLootClients(eachmoney);
                }
                if(remainder)
                {
                    psSystemMessage loot2(client->GetClientNum(),MSG_LOOT,"You have looted an extra %s.",remstr.GetData() );
                    loot2.SendMessage();

                    client->GetCharacterData()->AdjustMoney(remmoney, false);
                }
            }
            else
            {
                psMoney m = psMoney(money).Normalized();
                csString mstr = m.ToUserString();

                psSystemMessage loot(client->GetClientNum(),MSG_LOOT,"You have looted %s.",mstr.GetData() );
                loot.SendMessage();

                client->GetCharacterData()->AdjustMoney(m, false);
            }
            chr->AddLootMoney(-money);  // zero out loot now
        }
        else
            Debug1(LOG_LOOT, client->GetClientNum(),"Mob has no money to loot.\n");
    }
}

void UserManager::HandleQuests(Client *client)
{
    psQuestListMessage quests;
    size_t count = client->GetActor()->GetCharacterData()->GetAssignedQuests(quests,client->GetClientNum() );

    if (count)
    {
        Debug3(LOG_QUESTS, client->GetClientNum(), "Sending %zu quests to player %s.\n", count, client->GetName());
        quests.SendMessage();
    }
    else
    {
        Debug1(LOG_QUESTS, client->GetClientNum(),"Client has no quests yet.\n");
    }
}

void UserManager::HandleGMEvents(Client* client)
{

    psGMEventListMessage gmEvents;
    size_t count = client->GetActor()->GetCharacterData()->GetAssignedGMEvents(gmEvents, client->GetClientNum());

    if (count)
    {
        Debug3(LOG_QUESTS, client->GetClientNum(),
            "Sending %zu events to player %s.\n",
            count, client->GetName() );
        gmEvents.SendMessage();
    }
    else
    {
        Debug1(LOG_QUESTS,
            client->GetClientNum(),
            "Client is not participating in a GM-Event.\n");
    }
}

void UserManager::HandleTraining(Client *client)
{
    if (!client->IsAlive())
    {
        psserver->SendSystemError(client->GetClientNum(),"You are dead, you cannot train your skills now");
        return;
    }

    // Check target is a Trainer
    gemObject *target = client->GetTargetObject();
    if (!target || !target->GetActorPtr())
    {
        psserver->SendSystemInfo(client->GetClientNum(),
            "No target selected for training!");
        return;
    }

    // Check range
    if (client->GetActor()->RangeTo(target) > RANGE_TO_SELECT)
    {
        psserver->SendSystemInfo(client->GetClientNum(),
            "You are not in range to train with %s.",target->GetCharacterData()->GetCharName());
        return;
    }

    if (!target->IsAlive())
    {
        psserver->SendSystemInfo(client->GetClientNum(),
            "Can't train with a dead trainer!");
        return;
    }

    if (client->GetActor()->GetMode() != PSCHARACTER_MODE_PEACE)
    {
        csString err;
        err.Format("You can't train while %s.", client->GetCharacterData()->GetModeStr());
        psserver->SendSystemInfo(client->GetClientNum(), err);
        return;
    }

    psCharacter * trainer = target->GetCharacterData();
    if (!trainer->IsTrainer())
    {
        psserver->SendSystemInfo(client->GetClientNum(),
            "%s isn't a trainer.",target->GetCharacterData()->GetCharName());
        return;
    }

    psserver->GetProgressionManager()->StartTraining(client,trainer);
}

void UserManager::HandleBanking(Client *client, csString accountType)
{
    // Check if target is a banker.
    gemObject *target = client->GetTargetObject();
    if (!target || !target->GetActorPtr() || !target->GetActorPtr()->GetCharacterData()->IsBanker())
    {
        psserver->SendSystemError(client->GetClientNum(), "Your target must be a banker!");
        return;
    }

    // Check range
    if (client->GetActor()->RangeTo(target) > RANGE_TO_SELECT)
    {
        psserver->SendSystemError(client->GetClientNum(),
            "You are not within range to interact with %s.",target->GetCharacterData()->GetCharName());
        return;
    }

    // Check that the banker is alive!
    if (!target->IsAlive())
    {
        psserver->SendSystemError(client->GetClientNum(), "You can't interact with a dead banker!");
        return;
    }

    // Make sure that we're not busy doing something else.
    if (client->GetActor()->GetMode() != PSCHARACTER_MODE_PEACE)
    {
        csString err;
        err.Format("You can't access your bank account while %s.", client->GetCharacterData()->GetModeStr());
        psserver->SendSystemError(client->GetClientNum(), err);
        return;
    }

    // Check which account we're trying to access.
    if (accountType.CompareNoCase("personal"))
    {
        // Open personal bank.
        psserver->GetBankManager()->GetSingleton().StartBanking(client, false);
        return;
    }
    else if (accountType.CompareNoCase("guild"))
    {
        // Open guild bank.
        psserver->GetBankManager()->GetSingleton().StartBanking(client, true);
        return;
    }
    else
    {
        psserver->SendSystemError( client->GetClientNum(), "Usage: /bank [personal|guild]" );
        return;
    }
}

void UserManager::GiveTip(int id)
{
    unsigned int max=CacheManager::GetSingleton().GetTipLength();
    unsigned int rnd = psserver->rng->Get(max);

    csString tip;
    CacheManager::GetSingleton().GetTipByID(rnd, tip);
    psserver->SendSystemInfo(id,tip.GetData());
}

void UserManager::GiveMOTD(int id)
{
    psserver->SendSystemInfo(id,psserver->GetMOTD());
}


void UserManager::ChallengeToDuel(psUserCmdMessage& msg,Client *client)
{
    int clientnum = client->GetClientNum();

    if (!client->IsAlive())
    {
        psserver->SendSystemError(client->GetClientNum(),"You are dead, you cannot challenge opponents now");
        return;
    }

    // Check target dead
    gemObject *target = client->GetTargetObject();
    if (!target)
    {
        psserver->SendSystemError(clientnum,"You don't have another player targeted.");
        return;
    }

    Client * targetClient = psserver->GetNetManager()->GetClient(target->GetClientID());
    if (!targetClient)
    {
        psserver->SendSystemError(clientnum, "You can challenge other players only");
        return;
    }

    // Distance
    if(client->GetActor()->RangeTo(target) > RANGE_TO_CHALLENGE)
    {
        psserver->SendSystemError(clientnum,target->GetName() + csString(" is too far away"));
        return;
    }

    if (!target->IsAlive())
    {
        psserver->SendSystemError(clientnum, "You can't challenge a dead person");
        return;
    }

    if (targetClient == client)
    {
        psserver->SendSystemError(clientnum, "You can't challenge yourself.");
        return;
    }

    // Check for pre-existing duel with this person
    if (client->IsDuelClient(target->GetClientID()))
    {
        psserver->SendSystemError(clientnum, "You have already agreed on this duel !");
        return;
    }

    // Challenge
    csString question;
    question.Format("%s has challenged you to a duel!  Click on Accept to allow the duel or Reject to ignore the challenge.",
        client->GetName() );
    PendingDuelInvite *invite = new PendingDuelInvite(client,
        targetClient,
        question);
    psserver->questionmanager->SendQuestion(invite);
}

void UserManager::AcceptDuel(PendingDuelInvite *invite)
{
    Client * inviteeClient = clients->Find(invite->clientnum);
    Client * inviterClient = clients->Find(invite->inviterClientNum);

    if (inviteeClient!=NULL  &&  inviterClient!=NULL)
    {
        inviteeClient->AddDuelClient( invite->inviterClientNum );
        inviterClient->AddDuelClient( invite->clientnum );

        // Target eachother and update their GUIs
        inviteeClient->SetTargetObject(inviterClient->GetActor(),true);
        inviterClient->SetTargetObject(inviteeClient->GetActor(),true);
    }
}

void PendingDuelInvite::HandleAnswer(const csString & answer)
{
    Client * client = psserver->GetConnections()->Find(clientnum);
    if (!client  ||  client->IsDuelClient(inviterClientNum))
        return;

    PendingInvite::HandleAnswer(answer);
    if (answer == "yes")
        psserver->usermanager->AcceptDuel(this);
}

void UserManager::YieldDuel(Client *client)
{
    if (client->GetActor()->GetMode() == PSCHARACTER_MODE_DEFEATED || !client->GetActor()->IsAlive())
        return;

    bool canYield = false;
    for (int i = 0; i < client->GetDuelClientCount(); i++)
    {
        Client *duelClient = psserver->GetConnections()->Find(client->GetDuelClient(i));
        if (duelClient && duelClient->GetActor() && duelClient->GetActor()->IsAlive() && duelClient->GetActor()->GetMode() != PSCHARACTER_MODE_DEFEATED)
        {
            canYield = true;
            if (duelClient->GetTargetObject() == client->GetActor())
                psserver->combatmanager->StopAttack(duelClient->GetActor());
            psserver->SendSystemOK(client->GetClientNum(), "You've yielded to %s!", duelClient->GetName());
            psserver->SendSystemOK(duelClient->GetClientNum(), "%s has yielded!", client->GetName());
        }
    }

    if (!canYield)
    {
        psserver->SendSystemOK(client->GetClientNum(), "You have no opponents to yield to!");
        return;
    }

    psSpareDefeatedEvent *evt = new psSpareDefeatedEvent(client->GetActor());
    psserver->GetEventManager()->Push(evt);
    client->GetActor()->SetMode(PSCHARACTER_MODE_DEFEATED);
}

void UserManager::SwitchAttackTarget(Client *targeter, Client *targeted )
{
    // If we switch targets while in combat, start attacking the new
    // target, unless we no longer have a target.
    if (targeted)
        Attack(targeter->GetCharacterData()->GetCombatStance(), targeter, targeter->GetClientNum());
    else
        psserver->combatmanager->StopAttack(targeter->GetActor());
}

void UserManager::Teleport( Client *client, float x, float y, float z, int instance, float rot, const char* sectorname )
{
    csVector3 pos( x,y,z );
    csRef<iEngine> engine = csQueryRegistry<iEngine> (psserver->GetObjectReg());
    iSector * sector = engine->GetSectors()->FindByName(sectorname);
    if ( !sector )
    {
        Bug2("Sector %s is not found!", sectorname );
        return;
    }
    if ( !client->GetActor() )
    {
        Bug1("Actor for client not found!" );
        return;
    }

    client->GetActor()->SetInstance(instance);
    client->GetActor()->Move( pos, rot, sector );
    client->GetActor()->SetPosition( pos, rot, sector );
    client->GetActor()->UpdateProxList(true);  // true=force update
    client->GetActor()->MulticastDRUpdate();
}




/*---------------------------------------------------------------------*/

psUserStatRegeneration::psUserStatRegeneration(UserManager *mgr, csTicks ticks)
: psGameEvent(ticks,0,"psUserStatRegeneration")
{
    usermanager = mgr;
}

void psUserStatRegeneration::Trigger()
{
    usermanager->UserStatRegeneration();
}
