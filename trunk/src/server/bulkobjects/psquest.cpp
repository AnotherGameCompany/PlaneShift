/*
 * psquest.cpp
 *
//  * Copyright (C) 2003 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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

#include <iutil/document.h>
#include <csutil/xmltiny.h>

#include "util/log.h"
#include "util/strutil.h"
#include "util/consoleout.h"

#include "../globals.h"
#include "../psserver.h"
#include "../cachemanager.h"

#include "psquest.h"
#include "psquestprereqops.h"
#include "dictionary.h"


psQuest::psQuest()
{
    id = 0;
    parent_quest = NULL;
    image = NULL;
    step_id = 0;
    player_lockout_time = 0;
    quest_lockout_time = 0;
    quest_last_activated = 0;
    prerequisite = NULL;
    
    active = true;
}

psQuest::~psQuest()
{
    if (prerequisite)
        delete prerequisite;
    
    prerequisite = NULL;

    for(size_t i = 0;i < triggerPairs.GetSize(); i++)
    {
        TriggerResponse triggerResponse = triggerPairs.Get(i);
        dict->DeleteTriggerResponse(triggerResponse.trigger, triggerResponse.responseID);
    }

    // delete subquests
    for(size_t i = 0;i < subquests.GetSize(); i++)
        CacheManager::GetSingleton().UnloadQuest(subquests.Get(i));

}

void psQuest::Init(int new_id, const char *new_name)
{
    id   = new_id;
    name = new_name;
}

bool psQuest::Load(iResultRow& row)
{
    id   = row.GetInt("id");
    name = row["name"];
    task = row["task"];
    flags = row.GetInt("flags");
    step_id = row.GetInt("minor_step_number");
    category = row["category"];
    prerequisiteStr = row["prerequisite"];

    parent_quest = CacheManager::GetSingleton().GetQuestByID( row.GetUInt32("master_quest_id") );

    image = CacheManager::GetSingleton().FindCommonString( row.GetUInt32("cstr_id_icon") );

    // the value is expressed in seconds
    int lockout_time = row.GetInt("player_lockout_time");
    if (lockout_time < 0)
    {
        infinitePlayerLockout = true;
        player_lockout_time = 0;
    }
    else
    {
        infinitePlayerLockout = false;
        player_lockout_time = lockout_time;
    }
    
    quest_lockout_time  = row.GetInt("quest_lockout_time");

    return true;
}

bool LoadPrerequisiteXML(iDocumentNode * topNode, psQuest * self, psQuestPrereqOp*&prerequisite)
{
    // Recursively decode the xml document and generate prerequisite operators.

    if ( strcmp( topNode->GetValue(), "pre" ) == 0 )
    {
        // This is the top node. Only one node is legal.

        csRef<iDocumentNodeIterator> iter = topNode->GetNodes();

        while ( iter->HasNext() )
        {
            csRef<iDocumentNode> node = iter->Next();
            
            if ( node->GetType() != CS_NODE_ELEMENT )
                continue;

            if (!LoadPrerequisiteXML(node, self, prerequisite))
            {
                return false;
            }

            break;
        }
    } 
    else if ( strcmp( topNode->GetValue(), "completed" ) == 0 )
    {
        if (topNode->GetAttributeValue("quest"))
        {
            csString name = topNode->GetAttributeValue("quest");
            psQuest * quest;
            if (name == "self")
            {
                quest = self;
            } else
            {
                quest = CacheManager::GetSingleton().GetQuestByName( name );
            }
            if (quest)
            {
                prerequisite = new psQuestPrereqOpQuestCompleted(quest);
            }
            else
            {
                Error2("Can't find quest %s while loading prerequisite script",
                       topNode->GetAttributeValue("quest"));
            }
        }
        else if (topNode->GetAttributeValue("category"))
        {
            int min = -1,max = -1;
            csString category = topNode->GetAttributeValue("category");
            if (topNode->GetAttributeValue("min")) min = topNode->GetAttributeValueAsInt("min");
            if (topNode->GetAttributeValue("max")) max = topNode->GetAttributeValueAsInt("max");
            if (min == -1 && max == -1)
            {
                Error1("Both min and max is -1, seting min to 1");
                min = 1;
            }
            prerequisite = new psQuestPrereqOpQuestCompletedCategory(category,min,max);
        }
        else
        {
            Error1("No attrib of quest or category in the completed node.");
            return false;
        }
    }
    else if ( strcmp( topNode->GetValue(), "assigned" ) == 0 )
    {
        csString name = topNode->GetAttributeValue("quest");
        psQuest * quest;
        if (name == "self")
        {
            quest = self;
        } else
        {
            quest = CacheManager::GetSingleton().GetQuestByName( name );
        }
        if (quest)
        {
            prerequisite = new psQuestPrereqOpQuestAssigned(quest);
        }
        else
        {
            Error2("Can't find quest %s while loading prerequisite script",
                   topNode->GetAttributeValue("quest"));
            return false;
        }
    } 
    else if ( strcmp( topNode->GetValue(), "and" ) == 0 )
    {
        psQuestPrereqOpList * list = new psQuestPrereqOpAnd();
        prerequisite = list;
        
        csRef<iDocumentNodeIterator> iter = topNode->GetNodes();

        while ( iter->HasNext() )
        {
            csRef<iDocumentNode> node = iter->Next();
            
            if (node->GetType() != CS_NODE_ELEMENT)
                continue;

            psQuestPrereqOp * op = NULL;
            
            if (!LoadPrerequisiteXML(node, self, op))
            {
                return false;
            }
            
            if (op)
            {
                list->Push(op);
            }
            else
                return false;
        }
    }
    else if ( strcmp( topNode->GetValue(), "or" ) == 0 )
    {
        psQuestPrereqOpList * list = new psQuestPrereqOpOr();
        prerequisite = list;
        
        csRef<iDocumentNodeIterator> iter = topNode->GetNodes();

        while ( iter->HasNext() )
        {
            csRef<iDocumentNode> node = iter->Next();
            
            if ( node->GetType() != CS_NODE_ELEMENT )
                continue;

            psQuestPrereqOp * op = NULL;
            
            if (!LoadPrerequisiteXML(node, self, op))
            {
                return false;
            }
            
            if (op)
            {
                list->Push(op);
            }
            else
                return false;
        }
    }
    else if ( strcmp( topNode->GetValue(), "xor" ) == 0 )
    {
        psQuestPrereqOpList * list = new psQuestPrereqOpXor();
        prerequisite = list;
        
        csRef<iDocumentNodeIterator> iter = topNode->GetNodes();

        while ( iter->HasNext() )
        {
            csRef<iDocumentNode> node = iter->Next();
            
            if ( node->GetType() != CS_NODE_ELEMENT )
                continue;

            psQuestPrereqOp * op = NULL;
            
            if (!LoadPrerequisiteXML(node, self, op))
            {
                return false;
            }
            
            if (op)
            {
                list->Push(op);
            }
            else
                return false;
        }
    }
    else if ( strcmp( topNode->GetValue(), "require" ) == 0 )
    {
        int min = -1,max = -1;
        if (topNode->GetAttributeValue("min")) 
            min = topNode->GetAttributeValueAsInt("min");
        if (topNode->GetAttributeValue("max")) 
            max = topNode->GetAttributeValueAsInt("max");

        psQuestPrereqOpList * list = new psQuestPrereqOpRequire(min,max);
        prerequisite = list;
        
        csRef<iDocumentNodeIterator> iter = topNode->GetNodes();

        while ( iter->HasNext() )
        {
            csRef<iDocumentNode> node = iter->Next();
            
            if ( node->GetType() != CS_NODE_ELEMENT )
            continue;

            psQuestPrereqOp * op = NULL;
            
            if (!LoadPrerequisiteXML(node, self, op))
            {
                return false;
            }
            if (op)
            {
                list->Push(op);
            }
        }
    }
    else if ( strcmp( topNode->GetValue(), "faction" ) == 0 )
    {
        csString name = topNode->GetAttributeValue("name");
        if (name == "")
        {
            Error1("No name given for faction prerequisite operation");
            return false;
        }
        
        Faction * faction = CacheManager::GetSingleton().GetFaction(name);
        if (!faction)
        {
            Error2("Can't find faction '%s' for faction prerequisite operation",name.GetDataSafe());
            return false;
        }

        int value = topNode->GetAttributeValueAsInt("value");

        int max = topNode->GetAttributeValueAsInt("max");

        prerequisite = new psQuestPrereqOpFaction(faction,value,max);

    }
    else if ( strcmp( topNode->GetValue(), "activemagic" ) == 0 )
    {
        csString name = topNode->GetAttributeValue("name");
        if (name == "")
        {
            Error1("No name given for activemagic prerequisite operation");
            return false;
        }
        
        prerequisite = new psQuestPrereqOpActiveMagic(name);
    }
    else if ( strcmp( topNode->GetValue(), "timeofday" ) == 0 )
    {
        int min = topNode->GetAttributeValueAsInt("min");

        int max = topNode->GetAttributeValueAsInt("max");

        prerequisite = new psQuestPrereqOpTimeOfDay(min,max);

    }
    else if ( strcmp( topNode->GetValue(), "not" ) == 0 )
    {
        csRef<iDocumentNodeIterator> iter = topNode->GetNodes();

        while ( iter->HasNext() )
        {
            csRef<iDocumentNode> node = iter->Next();
            
            if ( node->GetType() != CS_NODE_ELEMENT )
            continue;

            psQuestPrereqOp * op;
            if (!LoadPrerequisiteXML(node, self, op))
            {
                return false;
            }
            psQuestPrereqOpList* list = new psQuestPrereqOpNot();
            list->Push(op);
            prerequisite = list;
            break;
        }
    } 
    else
    {
        Error2("Node \"%s\" isn't supported in  prerequisite scripts",topNode->GetValue());
        return false;
    }
    return true;
}


bool LoadPrerequisiteXML(psQuestPrereqOp*&prerequisite, psQuest * self, csString script)
{
    csRef<iDocumentSystem> xml = csPtr<iDocumentSystem>(new csTinyDocumentSystem);
    csRef<iDocument> doc = xml->CreateDocument();
    const char* error = doc->Parse( script );
    if ( error )
    {
        Error3("%s\n%s",error, script.GetDataSafe() );
        return false;
    }

    csRef<iDocumentNode> root    = doc->GetRoot();
    if(!root)
    {
        Error1("No XML root in prerequisite script");
        return false;
    }
    csRef<iDocumentNode> topNode = root->GetNode("pre");
    if (topNode)
    {
        return LoadPrerequisiteXML(topNode,self,prerequisite);
    }
    else
    {
        Error3("Could not find <pre> tag in prerequisite script '%s' for quest '%s'!",
               script.GetData(),(self?self->GetName():"(null)"));
        return false;
    }    

    return true;
}



bool psQuest::PostLoad()
{
    // Parse the prerequisite string
    if (prerequisiteStr != "")
    {
        Debug2(LOG_QUESTS,0, "Loading prereq  : %s", prerequisiteStr.GetDataSafe());

        if (!LoadPrerequisiteXML(prerequisite,this,prerequisiteStr))
        {
            Error1("Failed to load prerequistie XML");
            return false;
        }
        
        prerequisiteStr.Empty();
        if (prerequisite)
        {
            Debug2(LOG_QUESTS, 0, "Resulting prereq: %s\n", prerequisite->GetScript().GetDataSafe());
        }
        else
            return false;
    }

    return true;
}

bool psQuest::AddPrerequisite(csString prerequisitescript)
{
    psQuestPrereqOp * op;
    if (!LoadPrerequisiteXML(op,this,prerequisitescript))
    {
        return false;
    }

    if (op) 
    {
        return AddPrerequisite(op);
    }
    
    return false;
}


bool psQuest::AddPrerequisite(psQuestPrereqOp * op)
{
    // Make sure that the first op is an AND list if there are an
    // prerequisite from before.
    if (prerequisite)
    {
        // Check if first op is an and list.
        psQuestPrereqOpAnd * list = dynamic_cast<psQuestPrereqOpAnd*>(prerequisite);
        if (list == NULL)
        {
            // If not insert an and list.
            list = new psQuestPrereqOpAnd();
            list->Push(prerequisite);
            prerequisite = list;
        }

        list->Push(op);
    }
    else
    {
        // No prerequisite from before so just set this.
        prerequisite = op;
    }

    return true;
}

void psQuest::AddTriggerResponse(NpcTrigger * trigger, int responseID)
{
    TriggerResponse triggerPair;
    triggerPair.responseID = responseID;
    triggerPair.trigger    = trigger;
    triggerPairs.Push(triggerPair);
}

csString psQuest::GetPrerequisiteStr()
{
    if (prerequisite)
        return prerequisite->GetScript();

    return "";
}
