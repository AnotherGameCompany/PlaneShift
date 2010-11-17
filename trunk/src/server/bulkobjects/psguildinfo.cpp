/*
* psguildinfo.cpp
*
* Copyright (C) 2003 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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
//=============================================================================
// Crystal Space Includes
//=============================================================================

//=============================================================================
// Project Includes
//=============================================================================
#include "util/psdatabase.h"
#include "util/log.h"
#include "util/psconst.h"

#include "../psserver.h"
#include "../cachemanager.h"
#include "../playergroup.h"
#include "../guildmanager.h"
#include "../gem.h"
#include "../globals.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "psguildinfo.h"
#include "pscharacter.h"


/*******************************************************************************
*
*                                  Guilds
*
*******************************************************************************/


psGuildInfo::psGuildInfo()
{
}

psGuildInfo::psGuildInfo(csString name, PID founder)
:id(0),name(name),founder(founder),karma_points(0),secret(false),alliance(0)
{
}

psGuildInfo::~psGuildInfo()
{
    while (levels.GetSize())
        delete levels.Pop();
    while (members.GetSize())
        delete members.Pop();
}

bool psGuildInfo::Load(unsigned int id)
{
    if (!id)
    {
        return false;
    }
    Result result(db->Select("select * from guilds where id=%d",id));

    if (!result.IsValid() || result.Count() != 1)
    {
        Error2("Could not load guild id=%u.\n",id);
        return false;
    }

    return Load(result[0]);
}

bool psGuildInfo::Load(iResultRow& row)
{
    motd=row["motd"];
    id = row.GetInt("id");
    name = row["name"];
    founder = row.GetUInt32("char_id_founder");
    karma_points = row.GetInt("karma_points");
    web_page = row["web_page"];
    alliance = row.GetInt("alliance");

    if (row["secret_ind"])
        secret = (*row["secret_ind"]=='Y');

    max_guild_points = row.GetInt("max_guild_points");

    // Now get levels
    Result result(db->Select("select * from guildlevels where guild_id=%d order by level",id));
    if (!result.IsValid())
    {
        Error2("Could not load guild levels for guild %d.\n",id);
        return false;
    }
    unsigned int i;
    for (i=0; i<result.Count(); i++)
    {
        psGuildLevel *gl = new psGuildLevel;
        gl->level      = result[i].GetInt("level");
        gl->privileges = result[i].GetInt("rights");
        gl->title      = result[i]["level_name"];
        levels.Push(gl);
    }

    // Now preload members
    Result member(db->Select("select id,name,guild_level,guild_points,guild_public_notes,guild_private_notes, last_login, guild_additional_privileges, guild_denied_privileges"
        "  from characters"
        " where guild_member_of=%d",id));

    if (!member.IsValid())
    {
        Error2("Could not load guild members for guild %d.\n",id);
        return false;
    }


    for (i=0; i<member.Count(); i++)
    {
        psGuildMember *gm = new psGuildMember;

        gm->char_id = PID(member[i].GetUInt32("id"));
        gm->name    = NormalizeCharacterName(member[i]["name"]);

        gm->actor         = NULL;
        gm->guildlevel    = FindLevel(member[i].GetInt("guild_level"));

        gm->guild_points         = member[i].GetInt("guild_points");
        gm->public_notes         = member[i]["guild_public_notes"];
        gm->private_notes        = member[i]["guild_private_notes"];
        gm->last_login           = member[i]["last_login"];
        gm->privileges           = member[i].GetInt("guild_additional_privileges");
        gm->removedPrivileges    = member[i].GetInt("guild_denied_privileges");

        if (!gm->guildlevel)
        {
            Error2("Could not find guild level for %s.\n", ShowID(gm->char_id));
            delete gm;
            continue;
        }
        members.Push(gm);
    }

    // Load bank money.
    bankMoney.Set(row.GetInt("bank_money_circles"), row.GetInt("bank_money_octas"), row.GetInt("bank_money_hexas"), row.GetInt("bank_money_trias"));

    Result wars(db->Select("SELECT * FROM guild_wars WHERE guild_a = %d", id));
    if (wars.IsValid())
    {
        for (unsigned int i = 0 ; i < wars.Count() ; i++)
            guild_war_with_id.Push(wars[i].GetInt("guild_b"));
    }

    psserver->GetGuildManager()->CheckMinimumRequirements(this,NULL);

    return true;
}

bool psGuildInfo::InsertNew(PID leader_char_id)
{
    csString guildEsc;

    db->Escape(guildEsc,this->name);


    if (db->Command("insert into guilds (name,char_id_founder,date_created,max_guild_points) "
        "values ('%s', %d, Now(), %d )",
        guildEsc.GetData(),
        leader_char_id.Unbox(),
        DEFAULT_MAX_GUILD_POINTS)
        != 1)
    {
        Error3 ("Couldn't create new guild.\nCommand was "
            "<%s>.\nError returned was <%s>\n",db->GetLastQuery(),db->GetLastError());
        return false;
    }

    id = db->GetLastInsertID();

    for (int i=0; i<MAX_GUILD_LEVEL; i++)
    {
        csString level;
        if ( i == MAX_GUILD_LEVEL-1 )
        {
            level.Replace( "Guild Master");
        }
        else
        {
            level.Format("Level %d", i+1 );
        }

        csString escGuildLevel;
        db->Escape( escGuildLevel, level );

        if (db->Command("insert into guildlevels "
            "(guild_id,level,level_name,rights) "
            "values (%d, %d, '%s',%d )",
            id, i+1, escGuildLevel.GetData(), (i==MAX_GUILD_LEVEL-1)?0xFFFF:0x3 ) != 1)
        {
            Error4("Couldn't create guild level %d.\nCommand was "
                "<%s>.\nError returned was "
                "<%s>\n",i+1,
                db->GetLastQuery(),db->GetLastError());
            return false;
        }
        else
        {
            psGuildLevel *lev = new psGuildLevel;
            lev->level = i+1;
            lev->title = level;
            lev->privileges = (i==MAX_GUILD_LEVEL-1)?0xFF:0x3;

            levels.Push(lev);
        }
    }
    return true;
}

psGuildLevel *psGuildInfo::FindLevel(int level)
{
    for (size_t i=0; i<levels.GetSize(); i++)
    {
        if (levels[i]->level == level)
            return levels[i];
    }
    return NULL;
}

psGuildMember *psGuildInfo::FindMember(PID char_id)
{
    for (size_t i=0; i<members.GetSize(); i++)
    {
        if (members[i]->char_id == char_id)
            return members[i];
    }
    return NULL;
}

psGuildMember * psGuildInfo::FindLeader()
{
    for (size_t i=0; i<members.GetSize(); i++)
    {
        if (members[i]->guildlevel->level == MAX_GUILD_LEVEL)
            return members[i];
    }
    return NULL;
}

void psGuildInfo::Connect(psCharacter *player)
{
    for (size_t i=0; i<members.GetSize(); i++)
    {
        if (members[i]->char_id == player->GetPID())
        {
            player->SetGuild(this);
            members[i]->actor = player;
        }
    }
}

void psGuildInfo::Disconnect(psCharacter *player)
{
    // remove connection player <---> guild
    for (size_t i=0; i<members.GetSize(); i++)
    {
        if (members[i]->actor == player)
        {
            player->SetGuild(NULL);
            members[i]->actor = NULL;
            break;
        }
    }
}

void psGuildInfo::UpdateLastLogin(psCharacter *player)
{
    //updates last login informations for this member
    for (size_t i=0; i<members.GetSize(); i++)
    {
        if (members[i]->actor == player)
        {
            members[i]->last_login = player->GetLastLoginTime();
            break;
        }
    }
}

bool psGuildInfo::AddNewMember(psCharacter *player, int level)
{
    if (player->GetGuild())
    {
        Error1("Player is already member of a guild!");
        return false;      // still member of old guild so can't join
    }

    // Set player's guild
    if (db->Command("update characters "
        "   set guild_member_of=%d, "
        "       guild_level=%d,"
        "       guild_points=0,"
        "       guild_public_notes=NULL,"
        "       guild_private_notes=NULL"
        " where id=%u", id, level, player->GetPID().Unbox()) != 1)
    {
        Error3("Couldn't update guild for player.\nCommand was <%s>.\nError returned was <%s>\n",
            db->GetLastQuery(),db->GetLastError());
        return false;
    }

    psGuildMember *gm = new psGuildMember;

    gm->char_id = player->GetPID();
    gm->actor   = NULL;
    gm->guild_points = 0;
    gm->guildlevel = FindLevel(level);
    gm->name = player->name;
    gm->privileges = 0;
    gm->removedPrivileges = 0;

    members.Push(gm);

    Connect(player);
    UpdateLastLogin(player);

    return true;
}

psGuildMember *psGuildInfo::FindMember(const char *name)
{
    for (size_t i=0; i<members.GetSize(); i++)
    {
        if (members[i]->name.CompareNoCase(name))
            return members[i];
    }
    return NULL;
}

bool psGuildInfo::RemoveMember(psGuildMember *target)
{
    size_t i = members.Find(target);

    if (i==csArrayItemNotFound)
        return false;

    if (db->Command("update characters"
                    "   set guild_member_of=0,"
                    "       guild_level=0,"
                    "       guild_points=0,"
                    "       guild_public_notes=NULL,"
                    "       guild_private_notes=NULL"
                    " where id=%u", target->char_id.Unbox()) <= 0)
    {
        Error3("Couldn't update players to not be guild "
            "members.\nCommand was <%s>.\nError returned was "
            "<%s>\n",
            db->GetLastQuery(),db->GetLastError());
        return false;
    }

    if (target->actor)
        Disconnect(target->actor);

    members.DeleteIndex(i);
    return true;
}

bool psGuildInfo::RemoveGuild()
{
    if (alliance != 0)
    {
        psGuildAlliance * allianceObj = psserver->GetCacheManager()->FindAlliance(alliance);
        if (allianceObj != NULL)
            allianceObj->RemoveMember(this);
        else
            Error2("Couldn't find guild alliance in cachemanager %i", alliance);
    }

    // No check - Might not delete anything
    db->Command("delete from guild_wars where guild_a=%d or guild_b=%d",id,id);

    if (db->Command("delete from guilds where id=%d",id) != 1)
    {
        Error3("Couldn't remove guild.\nCommand was <%s>.\nError "
            "returned was <%s>\n",
            db->GetLastQuery(),db->GetLastError());
        return false;
    }

    if (db->Command("delete from guildlevels where guild_id=%d",
        id) <= 0)
    {
        Error3("Couldn't remove guild levels.\nCommand was "
            "<%s>.\nError returned was <%s>\n",
            db->GetLastQuery(),db->GetLastError());
        return false;
    }

    if (db->Command("update characters "
        "   set guild_member_of=0,"
        "       guild_level=0,"
        "       guild_points=0,"
        "       guild_public_notes=NULL,"
        "       guild_private_notes=NULL"
        " where guild_member_of=%d",id) < 0)
    {
        Error3("Couldn't update players to not be guild "
            "members.\nCommand was <%s>.\nError returned was "
            "<%s>\n",
            db->GetLastQuery(),db->GetLastError());
        return false;
    }

    return true;
}

bool psGuildInfo::RenameLevel(int level, const char *levelname)
{
    psGuildLevel *lev = FindLevel(level);

    if (lev)
    {
        lev->title = levelname;
        csString escLevel;
        db->Escape( escLevel, levelname );

        unsigned long res = db->Command("update guildlevels"
            "   set level_name='%s'"
            " where guild_id=%d"
            "   and level=%d",
            escLevel.GetData(), id, level);

        if (res == QUERY_FAILED)
        {
            Error2("sql error: %s", db->GetLastError());
            return false;
        }
        return true;
    }
    else
        return false;
}


const char* psGuildInfo::GetMOTD() {
    return (const char*)motd.GetData();
}

bool psGuildInfo::SetMOTD(const char* str)
{

    csString escMOTD;
    db->Escape( escMOTD, str );
    unsigned long res = db->Command("update guilds"
        "   set motd='%s'"
        " where id=%d",
        escMOTD.GetData(), id);

    if (res == QUERY_FAILED)
    {
        Error2("sql error: %s", db->GetLastError());
        return false;
    }
    motd = str;
    return true;
}

bool psGuildInfo::SetPrivilege(int level, GUILD_PRIVILEGE privilege, bool on)
{
    psGuildLevel *lev = FindLevel(level);

    if (lev)
    {
        if (on)
        {
            lev->privileges |= privilege;
        } else
        {
            lev->privileges &= ~privilege;
        }


        unsigned long res = db->Command("update guildlevels"
            "   set rights='%d'"
            " where guild_id=%d"
            "   and level=%d",
            lev->privileges,id,level);

        if (res == QUERY_FAILED)
        {
            Error2("sql error: %s", db->GetLastError());
            return false;
        }
        return true;
    }
    else
        return false;
}

bool psGuildInfo::SetMemberPrivilege(psGuildMember *member, GUILD_PRIVILEGE privilege, bool on)
{
    psGuildLevel *lev = member->guildlevel;

    if (lev)
    {
        if (on)
        {
            if(!member->HasRights(privilege)) //we operate only if the character doesn't have the privilege
            {
                if(lev->HasRights(privilege)) //should the character have the right because of his level?
                {
                    //if so remove this privilege among the specially denied privileges
                    member->removedPrivileges &= ~privilege;
                }
                else //we are adding it as a special privilege for the character
                {
                    member->privileges |= privilege;
                }
            }
            else
            {
                return false; //nothing to do here
            }
        }
        else
        {
            if(member->HasRights(privilege)) //we operate only if the character has the privilege
            {
                if(lev->HasRights(privilege)) //does the character have the right because of his level?
                {
                    //if so add this privilege among the specially denied privileges
                    member->removedPrivileges |= privilege;
                }
                else //it was a special privilege the character had, so just remove it
                {
                    member->privileges &= ~privilege;
                }
            }
            else
            {
                return false; //nothing to do the character doesn't have the privilege
            }
        }

        unsigned long res = db->Command("update characters"
            "   set guild_additional_privileges='%d', guild_denied_privileges='%d'"
            " where id = '%d'",
            member->privileges, member->removedPrivileges, member->char_id.Unbox());

        if (res == QUERY_FAILED)
        {
            Error2("sql error: %s", db->GetLastError());
            return false;
        }
        return true;
    }
    else
        return false;
}

bool psGuildInfo::UpdateMemberLevel(psGuildMember *target,int level)
{
    psGuildLevel *lev = FindLevel(level);

    if (lev)
    {
        target->guildlevel = lev;

        unsigned long res = db->Command("UPDATE characters SET guild_level=%d WHERE id=%u", level, target->char_id.Unbox());

        if (res == QUERY_FAILED)
        {
            Error2("sql error: %s", db->GetLastError());
            return false;
        }

        return true;
    }
    return false;
}

void psGuildInfo::AdjustMoney(psMoney money, bool)
{
    bankMoney.AdjustCircles(money.GetCircles());
    bankMoney.AdjustOctas(money.GetOctas());
    bankMoney.AdjustHexas(money.GetHexas());
    bankMoney.AdjustTrias(money.GetTrias());
    SaveBankMoney();
}

void psGuildInfo::SaveBankMoney()
{
    psString sql;

    sql.AppendFmt("update guilds set bank_money_circles=%u, bank_money_trias=%u, bank_money_hexas=%u, bank_money_octas=%u where id=%u",
                  bankMoney.GetCircles(), bankMoney.GetTrias(), bankMoney.GetHexas(), bankMoney.GetOctas(), id);
    if (db->Command(sql) != 1)
    {
        Error3 ("Couldn't save guild's bank money to database.\nCommand was "
            "<%s>.\nError returned was <%s>\n",db->GetLastQuery(),db->GetLastError());
    }
}

bool psGuildInfo::SetMemberPoints(psGuildMember * member, int points)
{
    if (points > max_guild_points)
        return false;

    member->guild_points = points;

    unsigned long res = db->Command("UPDATE characters SET guild_points=%d WHERE id=%u", points, member->char_id.Unbox());
    if (res != 1)
    {
        Error3("Couldn't save guild_points of %s: %s", ShowID(member->char_id), db->GetLastError());
        return false;
    }
    return true;
}

bool psGuildInfo::SetMaxMemberPoints(int points)
{
    if (points > MAX_GUILD_POINTS_LIMIT)
        points = MAX_GUILD_POINTS_LIMIT;

    max_guild_points = points;

    unsigned long res = db->Command("UPDATE guilds SET max_guild_points=%d WHERE id=%u", points, id);
    if (res != 1)
    {
        Error3("Couldn't save max guild_points of %d: %s", id, db->GetLastError());
        return false;
    }
    return true;
}

bool psGuildInfo::SetMemberNotes(psGuildMember * member, const csString & notes, bool isPublic)
{
    const char * dbName;

    if (isPublic)
    {
        member->public_notes = notes;
        dbName = "guild_public_notes";
    }
    else
    {
        member->private_notes = notes;
        dbName = "guild_private_notes";
    }

    csString escNotes;
    db->Escape( escNotes, notes );
    unsigned long res = db->Command("UPDATE characters SET %s='%s' WHERE id=%u", dbName, escNotes.GetData(), member->char_id.Unbox());
    if (res != 1)
    {
        Error4("Couldn't save %s of %s: %s", dbName, ShowID(member->char_id), db->GetLastError());
        return false;
    }
    else
        return true;
}



bool psGuildInfo::SetName(csString guildName)
{
    if (name == guildName)
        return true;

    name = guildName;

    csString escGuildName;
    db->Escape( escGuildName, guildName );
    unsigned long res = db->Command("update guilds"
        "   set name='%s'"
        " where id=%d",
        escGuildName.GetData(),
        id);

    if (res == QUERY_FAILED)
    {
        Error2("sql error: %s", db->GetLastError());
        return false;
    }
    return true;
}

bool psGuildInfo::SetSecret(bool secretGuild)
{
    if (secret == secretGuild)
        return true;

    secret = secretGuild;

    unsigned long res = db->Command("update guilds"
        "   set secret_ind='%c'"
        " where id=%d",
        secret?'Y':'N',
        id);

    if (res == QUERY_FAILED)
    {
        Error2("sql error: %s", db->GetLastError());
        return false;
    }
    return true;
}

bool psGuildInfo::SetWebPage(const csString & web_page)
{
    if (this->web_page == web_page)
        return true;

    this->web_page = web_page;

    csString escWeb;
    db->Escape( escWeb, web_page );
    unsigned long res = db->Command("update guilds"
        "   set web_page='%s'"
        "   where id=%d", escWeb.GetData(), id);

    if (res == QUERY_FAILED)
    {
        Error2("sql error: %s", db->GetLastError());
        return false;
    }
    return true;
}

void psGuildInfo::AddGuildWar(psGuildInfo *other)
{
    guild_war_with_id.Push( other->id );
    db->CommandPump("INSERT INTO guild_wars VALUES(%d, %d)", id, other->id);
}

bool psGuildInfo::IsGuildWarActive(psGuildInfo *other)
{
    for (size_t i=0; i<guild_war_with_id.GetSize(); i++)
    {
        if (guild_war_with_id[i] == other->id)
            return true;
    }
    return false;
}

void psGuildInfo::RemoveGuildWar(psGuildInfo *other)
{
    guild_war_with_id.Delete(other->id);
    db->CommandPump("DELETE FROM guild_wars WHERE guild_a = %d and guild_b = %d", id, other->id);
}


/*******************************************************************************
*
*                              Guild alliances
*
*******************************************************************************/

csString psGuildAlliance::lastError;

psGuildAlliance::psGuildAlliance()
{
    id = 0;
    leader = NULL;
}

psGuildAlliance::psGuildAlliance(const csString & name)
{
    id = 0;
    this->name = name;
    leader = NULL;
}

bool psGuildAlliance::InsertNew()
{
    csString escName;
    db->Escape( escName, name );
    if (db->Command("insert into alliances (name, leading_guild) values ('%s', 0)", name.GetData()) != 1)
    {
        lastError = db->GetLastError();
        return false;
    }
    id = db->GetLastInsertID();
    return true;
}

bool psGuildAlliance::RemoveAlliance()
{
    for (size_t i=0; i < members.GetSize(); i++)
        members[i]->alliance = 0;

    db->Command("update guilds set alliance=0 where alliance=%d",id);
    db->Command("delete from alliances where id=%d",id);
    return true;
}

bool psGuildAlliance::AddNewMember(psGuildInfo * member)
{
    size_t index;

    index = members.Find(member);
    if (index != csArrayItemNotFound)
        return false;

    if (db->Command("update guilds set alliance=%d where id=%d",id,member->id) != 1)
    {
        lastError = db->GetLastError();
        return false;
    }

    members.Push(member);
    member->alliance = id;

    return true;
}

bool psGuildAlliance::CheckMembership(psGuildInfo * member)
{
    return members.Find(member) !=  csArrayItemNotFound;
}

bool psGuildAlliance::RemoveMember(psGuildInfo * member)
{
    size_t index;

    if (leader == member)
    {
        lastError = "You can't remove leader of alliance.";
        return false;
    }

    index = members.Find(member);
    if (index == csArrayItemNotFound)
    {
        lastError = "Guild not found in members";
        return false;
    }

    if (db->Command("update guilds set alliance=0 where id=%d",member->id) != 1)
    {
        lastError = db->GetLastError();
        return false;
    }

    members.DeleteIndex(index);
    member->alliance = 0;
    return true;
}

bool psGuildAlliance::Load(int id)
{
    psGuildInfo * member;
    int leaderID;
    unsigned int memberNum;

    this->id = id;

    // Load name and leader of alliance
    
    Result result(db->Select("select name, leading_guild from alliances where id=%d", id))
    if (!result.IsValid())
    {
        lastError = db->GetLastError();
        return false;
    }

    if (result.Count() == 0)
    {
        lastError = "Alliance not found in database";
        return false;
    }

    name = (result)[0]["name"];
    leaderID = (result)[0].GetInt("leading_guild");

    leader = psserver->GetCacheManager()->FindGuild(leaderID);
    if (leader == NULL)
    {
        lastError = "ID of leader read from alliances.leading_guild not found in cachemanager";
        return false;
    }


    // Load members of alliance

    Result resultMembers(db->Select("select id from guilds where alliance=%d order by name", id))
    if (!resultMembers.IsValid())
    {
        lastError = db->GetLastError();
        return false;
    }

    for (memberNum=0; memberNum < result->Count(); memberNum++)
    {
        member = psserver->GetCacheManager()->FindGuild(  (resultMembers)[memberNum].GetInt("id")  );
        if (member == NULL)
        {
            lastError = "Member of alliance loaded from DB couln't be found in cachemanager";
            return false;
        }
        members.Push(member);
    }
    return true;
}

bool psGuildAlliance::SetLeader(psGuildInfo * newLeader)
{
    if (    !     CheckMembership(newLeader))
        return false;

    if (db->Command("update alliances set leading_guild=%d where id=%d",newLeader->id, id) != 1)
    {
        lastError = db->GetLastError();
        return false;
    }

    leader = newLeader;

    return true;
}

size_t psGuildAlliance::GetMemberCount()
{
    return members.GetSize();
}

psGuildInfo * psGuildAlliance::GetMember(int memberNum)
{
    CS_ASSERT(memberNum>=0  &&  (size_t)memberNum<members.GetSize());
    return members[memberNum];
}

bool psGuildInfo::MeetsMinimumRequirements()
{
    if (members.GetSize() < GUILD_MIN_MEMBERS)
        return false;
    else
        return true;
}

