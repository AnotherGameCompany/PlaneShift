/*
* recipe.cpp             by Zee (RoAnduku@gmail.com)
*
* Copyright (C) 2011 Atomic Blue (info@planeshift.it, http://www.atomicblue.org) 
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
#include <stdlib.h>
//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/csstring.h>
#include <csutil/stringarray.h>
#include <csutil/xmltiny.h>
#include <iutil/object.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "util/psdatabase.h"
#include "util/log.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "recipe.h"
#include "npc.h"

Recipe::Recipe()
{
    id          = -1;
    name        = "";
    persistence = false;
    unique      = false;
}

bool Recipe::Load(iResultRow &row) 
{
    csStringArray unparsedReq;
    csString      reqText;
    int pers     = row.GetInt("persistent");
    int isUnique = row.GetInt("uniqueness");
    id           = row.GetInt("id");
    name         = row["name"];
    
    // Set persistance
    if(pers == 1) persistence = true;

    // Set unique
    if(isUnique == 1) unique = true;

    // Fetch the data
    algorithm.SplitString(row["algorithm"], ";");
    for(int i=0;i<algorithm.GetSize();i++)
    {
        if(strlen(algorithm.Get(i)) == 0)
        {
            algorithm.DeleteIndex(i);
        }
    }
    unparsedReq.SplitString(row["requirements"], ";");
    unparsedReq.DeleteIndex(unparsedReq.GetSize() - 1);

    // Parse Requirements
    for(int i=0;i<unparsedReq.GetSize();i++)
    {
        if(unparsedReq.Get(i) == "" ||
           unparsedReq.Get(i) == " ")
            continue; // Shouldn't happen ... but hell ?!
        
        // Split into function & argument 1 & argument 2
        csStringArray explodedReq;
        explodedReq.SplitString(unparsedReq.Get(i), "(,)");
        Requirement newReq;
        reqText = explodedReq.Get(0);
        
        // And check its contents
        if(reqText == "tribesman")
        {
            newReq.type = REQ_TYPE_TRIBESMAN;
        }
        else if(reqText == "resource")
        {
            newReq.type = REQ_TYPE_RESOURCE;
        }
        else if(reqText == "item")
        {
            newReq.type = REQ_TYPE_ITEM;
        }
        else if(reqText == "knowledge")
        {
            newReq.type = REQ_TYPE_KNOWLEDGE;
        }
        else if(reqText == "recipe")
        {
            newReq.type = REQ_TYPE_RECIPE;
        }
        else if(reqText == "trader")
        {
            newReq.type = REQ_TYPE_TRADER;
        }
        else if(reqText == "memory")
        {
            newReq.type = REQ_TYPE_MEMORY;
        }
        else
        {
            Error2("Unknown recipe requirement: %s. Bail.", explodedReq.Get(0));
            exit(1);
        }

        newReq.name = explodedReq.Get(1);
        newReq.quantity = explodedReq.Get(2);

        requirements.Push(newReq);
    }

    return true;
}

void Recipe::Dump() {
    CPrintf(CON_NOTIFY, "Dumping recipe %d.\n", id);
    CPrintf(CON_NOTIFY, "Name: %s \n", name.GetData());
    DumpAlgorithm();
    DumpRequirements();
    CPrintf(CON_NOTIFY, "Persistent: %d \n", persistence);
    CPrintf(CON_NOTIFY, "Unique: %d \n", unique);
}

void Recipe::DumpAlgorithm() 
{
    CPrintf(CON_NOTIFY, "Algorithm %d.\n", id);
    for(int i=0;i<algorithm.GetSize();i++)
        CPrintf(CON_NOTIFY, "%d) %s\n", (i+1), algorithm.Get(i));
}

void Recipe::DumpRequirements() 
{
    CPrintf(CON_NOTIFY, "Requirement %d.\n", id);
    for(int i=0;i<requirements.GetSize();i++)
        CPrintf(CON_NOTIFY, "%d) %s\n", (i+1), requirements[i].name.GetData());
}


RecipeManager::RecipeManager(psNPCClient* NPCClient, EventManager* eventManager) 
{
    npcclient    = NPCClient;
    eventmanager = eventManager;
}

csString RecipeManager::Preparse(csString function, Tribe* tribe)
{
    csString container;

    container = "";
    container.Append(tribe->GetBuffer("Buffer"));
    function.ReplaceAll("BUFFER", container.GetData());

    container = "";
    container.Append(tribe->GetBuffer("Active Amount"));
    function.ReplaceAll("ACTIVE_AMOUNT", container.GetData());

    container.Append(tribe->GetReproductionCost());
    function.ReplaceAll("REPRODUCTION_COST", container.GetData());

    container = "";
    container.Append(tribe->GetNeededResource());
    function.ReplaceAll("REPRODUCTION_RESOURCE", container.GetData());

    return function;
}

bool RecipeManager::LoadRecipes() 
{
    Result rs(db->Select("SELECT * FROM tribe_recipes"));

    if(!rs.IsValid())
    {
        Error2("Could not load recipes from the database. Reason: %s", db->GetLastError() );
        return false;
    }

    for(int i=0;i<(int)rs.Count();i++)
    {
        Recipe* newRecipe = new Recipe;
        recipes.Push(newRecipe);
        newRecipe->Load(rs[i]);
    }
    CPrintf(CON_NOTIFY, "Recipes loaded.\n");
    return true;
}

void RecipeManager::AddTribe(Tribe *tribe)
{
    TribeData newTribe;
    newTribe.tribeID = tribe->GetID();

    // Get Tribal Recipe
    newTribe.tribalRecipe = tribe->GetTribalRecipe();
        
    csStringArray tribeStats = newTribe.tribalRecipe->GetAlgorithm();
    csStringArray keywords;
    csString      keyword;
    
    // Get Tribe Stats
    for(int i=0;i<tribeStats.GetSize();i++)
    {
        keywords.SplitString(tribeStats.Get(i), "(,)");
        keyword = keywords.Get(0);

        if(keyword == "aggressivity")
        {
            newTribe.aggressivity = keywords.Get(1);
        } 
        else if(keyword == "brain")
        {
            newTribe.brain = keywords.Get(1);
        }
        else if(keyword == "growth")
        {
            newTribe.growth = keywords.Get(1);
        }
        else if(keyword == "unity")
        {
            newTribe.unity = keywords.Get(1);
        }
        else if(keyword == "sleepPeriod")
        {
            newTribe.sleepPeriod = keywords.Get(1);
        }
        else if(keyword == "loadRecipe")
        {
            Recipe* newRecipe = GetRecipe(csString(keywords.Get(1)));
            if(strncmp(keywords.Get(2), "distributed", 11) == 0)
            {
                tribe->AddRecipe(newRecipe, newTribe.tribalRecipe, true);
            } 
            else
            {
                tribe->AddRecipe(newRecipe, newTribe.tribalRecipe);
            }
        }
        else {
            Error2("Unknown tribe stat: '%s'. Abandon ship.", keywords[0]);
            exit(1);
        }

        keywords.DeleteAll();
    }

    // Add structure to array
    tribeData.Push(newTribe);
    
    // Add tribes pointer to RM's array
    tribes.Push(tribe);

    // Create Tribal NPCType
    CreateGlobalNPCType(tribe);
}

bool RecipeManager::ParseFunction(csString function, Tribe* tribe, csArray<NPC*>& npcs, Recipe* recipe)
{
    function = Preparse(function, tribe);

    // TODO -- Remove printf
    /* 
    printf("ZeeDebug: Parsing Function %s on members:\n", function.GetData());
    for(int i=0;i<npcs.GetSize();i++)
    {
        printf("PID: %d\n", npcs[i]->GetPID().Unbox());
    }
    */

    csStringArray functionParts;
    csString      functionBody;
    csStringArray functionArguments;

    functionParts.SplitString(function, "()");
    functionBody = functionParts.Get(0);
    functionArguments.SplitString(functionParts.Get(1), ",");
    
    // Due split, empty items may be in the array
    for(int i=0;i<functionArguments.GetSize();i++)
    {
        if(strlen(functionArguments.Get(i)) == 0)
            functionArguments.DeleteIndex(i);
    }

    int argSize = functionArguments.GetSize();
    
    // On each of the following conditions we check function number and arguments 
    // and then apply the function effect

    if(functionBody == "alterResource")
    {
        if(argSize != 2)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 2, argSize);
            return false;
        }
        tribe->AddResource(functionArguments.Get(0), atoi(functionArguments.Get(1)));
        return true;
    }
    else if(functionBody == "loadLocation")
    {
        if(argSize != 4)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 4, argSize);
            return false;
        }
        csVector3 whatLocation;
        whatLocation[0] = atoi(functionArguments.Get(0));
        whatLocation[1] = atoi(functionArguments.Get(1));
        whatLocation[2] = atoi(functionArguments.Get(2));
        tribe->AddMemory("work",whatLocation,
                        tribe->GetHomeSector(),
                        10,NULL);

        return true;
    }
    else if(functionBody == "goWork")
    {  
        if(argSize != 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 4, argSize);
            return false;
        }
        // Set the workers buffers with how many seconds they should work
        csString duration;
        duration.Append(atoi(functionArguments.Get(0)));
        printf("Work %s seconds.\n", duration.GetData());
        for(int i=0;i<npcs.GetSize();i++)
        {
            npcs[i]->SetBuffer(duration);
        }
        tribe->SendPerception("tribe:work", npcs);
        return true;
    }
    else if(functionBody == "bogus")
    {
        if(argSize != 0)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 0, argSize);
        }
        // The 'Slack' Function
        return true;
    }
    else if(functionBody == "loadCyclicRecipe")
    {
        if(argSize != 2)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 2, argSize);
            return false;
        }
        tribe->AddCyclicRecipe(GetRecipe(functionArguments.Get(0)),
                                   atoi(functionArguments.Get(1)));
    }
    else if(functionBody == "locateMemory" || functionBody == "locateResource")
    {
        if(argSize != 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 1, argSize);
            return false;
        }
        if(!tribe->LoadNPCMemoryBuffer(functionArguments.Get(0), npcs))
        {
            // No such memory, then explore for it.
            tribe->SetBuffer("Buffer", functionArguments.Get(0));
            tribe->AddRecipe(GetRecipe("Explore"), recipe);
            return false;
        }
        return true;
    }
    else if(functionBody == "locateBuildingSpot")
    {
        if(argSize != 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 1, argSize);
            return false;
        }
        
        Tribe::Asset* spot = tribe->GetBuildingSpot(functionArguments.Get(0));

        if(!spot)
        {
            // Panic, no spot reserved for this building
            return false;
        }

        spot->status = ASSET_INCONSTRUCTION;
        // Make a new memory and copy it to npcs
        Tribe::Memory memory;
        memory.name = functionArguments.Get(0);
        memory.pos  = spot->pos;
        memory.sector = tribe->GetHomeSector();
        memory.sectorName = tribe->GetHomeSectorName();
        memory.radius = 20; // Why ?

        tribe->LoadNPCMemoryBuffer(&memory,npcs);
    }
    else if(functionBody == "addKnowledge")
    {
        if(argSize != 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 1, argSize);
            return false;
        }
        tribe->AddKnowledge(functionArguments.Get(0));
        return true;
    }
    else if(functionBody == "reserveSpot")
    {
        if(argSize != 4)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 4, argSize);
            return false;
        }
        csVector3 where;
        const char* buildingName = functionArguments.Get(3);
        where[0] = atoi(functionArguments.Get(0));
        where[1] = atoi(functionArguments.Get(1));
        where[2] = atoi(functionArguments.Get(2));
        tribe->AddBuildingAsset(buildingName, where, ASSET_BUILDINGSPOT);
    }
    else if(functionBody == "addBuilding")
    {
        if(argSize != 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 1, argSize);
            return false;
        }
        tribe->SpawnBuilding(functionArguments.Get(0));
        return true;
    }
    else if(functionBody == "attack")
    {
        if(argSize != 0)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 0, argSize);
            return false;
        }
        tribe->SendPerception("tribe:attack", npcs);
        return true;
    }
    else if(functionBody == "gather")
    {
        if(argSize != 0)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 0, argSize);
            return false;
        }
        tribe->SendPerception("tribe:gather", npcs);
        return true;
    }
    else if(functionBody == "mine")
    {
        if(argSize != 0)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 0, argSize);
            return false;
        }
        tribe->SendPerception("tribe:mine", npcs);
        return true;
    }
    else if(functionBody == "select")
    {
        if(argSize != 2)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 2, argSize);
            return false;
        }
        npcs.Empty();
        npcs = tribe->SelectNPCs(functionArguments.Get(0), functionArguments.Get(1));
        return true;
    }
    else if(functionBody == "explore")
    {
        if(argSize != 0)
        {
            printf("(%s)\n",functionArguments.Get(0));
            DumpError(recipe->GetName(), functionBody.GetData(), 0, argSize);
            return false;
        }
        
        // TODO -- Make specific explorations
        // Tell them what to explore for
        /*
        for(int i=0;i<npcs.GetSize();i++)
        {
            npcs->SetBuffer(functionArguments.Get(0));
        }
        */

        tribe->SendPerception("tribe:explore", npcs);
        return true;
    }
    else if(functionBody == "setBuffer")
    {
        if(argSize != 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 1, argSize);
            return false;
        }
        tribe->SetBuffer("Buffer", functionArguments.Get(0));
        return true;
    }
    else if(functionBody == "setAmountBuffer")
    {
        if(argSize != 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 1, argSize);
            return false;
        }
        tribe->SetBuffer("Active Amount", functionArguments.Get(0));
        return true;
    }
    else if(functionBody == "wait")
    {
        if(argSize != 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 1, argSize);
            return false;
        }
        tribe->ModifyWait(recipe, atoi(functionArguments.Get(0)));
        // We return false to stop the execution
        return false;
    }
    else if(functionBody == "loadRecipe")
    {
        if(argSize < 1)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 1, argSize);
            return false;
        }
        if(argSize == 2 && (strncmp(functionArguments.Get(1),"distributed",11) == 0))
        {
            // We have a distributed requirements recipe
            tribe->AddRecipe(GetRecipe(functionArguments.Get(0)), recipe, true);
            return false;
        }
        tribe->AddRecipe(GetRecipe(functionArguments.Get(0)), recipe);
        // We return false because we want the loaded recipe to execute next, not the following steps
        return false;
    }
    else if(functionBody == "mate")
    {
        if(argSize != 0)
        {
            DumpError(recipe->GetName(), functionBody.GetData(), 0, argSize);
            return false;
        }
        tribe->SendPerception("tribe:breed", npcs);
        return true;
    }
    else
    {
        Error2("Unknown function primitive: %s. Bail.\n", functionBody.GetData());
        exit(1);
    }

    // Written just for common sense, will never get here... but who knows!
    return false;
}

bool RecipeManager::ParseRequirement(Recipe::Requirement requirement, Tribe* tribe, Recipe* recipe)
{
    csString name = Preparse(requirement.name, tribe);
    int      quantity = atoi(Preparse(requirement.quantity, tribe));

    // Check type of requirement and do something for each
    if(requirement.type == REQ_TYPE_TRIBESMAN)
    {
        if(tribe->CheckMembers(name, quantity))
            return true;

        // If Requirement is not met check to see
        // if more members can be spawned.
        // If they can be spawned... set the buffer to the required
        // type and add the generic recipe 'mate'
        // ~Same goes for other requirements~
        // Otherwise just wait for members to free.

        if(recipe->GetID()<5)
        {
            // Just wait for members to free for basic recipes
            return false;
        }
        
        if(tribe->CanGrow() && tribe->ShouldGrow())
        {
            // Start ... erm... cell-dividing :)
            tribe->SetBuffer("Buffer", name);
            tribe->AddRecipe(GetRecipe("mate"), recipe);
        }
        else if(tribe->ShouldGrow())
        {
            // Gather more resources
            tribe->SetBuffer("Buffer", tribe->GetNeededResource());
            bool digable = true; // Provisional TODO
            if(digable)
            {
                tribe->AddRecipe(GetRecipe("Dig Resource"), recipe);
            }
            else
            {
                tribe->AddRecipe(GetRecipe("Gather Resource"), recipe);
            }
        }
        return false;
    }
    else if(requirement.type == REQ_TYPE_RESOURCE)
    {
        if(tribe->CheckResource(name, quantity))
            return true;

        // Mine more resources of this kind
        // TODO -- Check to see if resource is diggable or gatherable

        tribe->SetBuffer("Buffer", name);
        bool digable = true; // Provisional TODO
        if(digable)
        {
            tribe->AddRecipe(GetRecipe("Dig Resource"), recipe);
        }
        else
        {
            tribe->AddRecipe(GetRecipe("Gather Resource"), recipe);
        }

        return false;
    }
    else if(requirement.type == REQ_TYPE_ITEM)
    {
        if(tribe->CheckItems(name, quantity))
            return true;
        
        for(int i=0;i<quantity;i++)
        {
            tribe->AddRecipe(GetRecipe(name), recipe);
        }
        return false;
    }
    else if(requirement.type == REQ_TYPE_KNOWLEDGE)
    {
        if(tribe->CheckKnowledge(name))
            return true;

        // Get this knowledge
        tribe->AddRecipe(GetRecipe(name), recipe);
        return false;
    }
    else if(requirement.type == REQ_TYPE_RECIPE)
    {
        Recipe* required = GetRecipe(name);
        tribe->AddRecipe(required, recipe);
        return true;
    }
    else if(requirement.type == REQ_TYPE_MEMORY)
    {
        if(tribe->FindMemory(name))
            return true;
        
        // We'll have to run a pre-check to check for unprospected
        // mines in case we need an ore we don't know
        if(tribe->FindMemory("mine"))
            return true;

        // Explore for not found memory
        tribe->AddRecipe(GetRecipe("Explore"), recipe);
        return false;
    }
    else
    {
        Error2("How did type %d did even become possible? Bail.", requirement.type);
        exit(1);
    }
}

void RecipeManager::CreateGlobalNPCType(Tribe* tribe)
{
    csString assembledType;
    csString reaction;
    // Get our tribe's data
    TribeData* currentTribe = GetTribeData(tribe);

    assembledType = "<npctype name=\"tribe_";
    assembledType.Append(tribe->GetID());
    assembledType.Append("\" parent=\"AbstractTribesman\">");

    // Aggressivity Trait
    if(currentTribe->aggressivity == "warlike")
    {
        reaction = "<react event=\"player nearby\" behavior=\"aggressive_meet\" delta=\"150\" />";
        reaction = "<react event=\"attack\" behavior=\"aggressive_meet\" delta=\"150\" />";
        assembledType += reaction;
    }
    else if(currentTribe->aggressivity == "pacifist")
    {
        reaction = "<react event=\"player nearby\" behavior=\"peace_meet\" delta=\"100\" />\n";
        reaction += "<react event=\"attack\" behavior=\"normal_attacked\" delta=\"150\" />";
        assembledType += reaction;
    }
    else if(currentTribe->aggressivity == "coward")
    {
        reaction = "<react event=\"player nearby\" behavior=\"peace_meet\" delta=\"100\" />\n";
        reaction += "<react event=\"attack\" behavior=\"coward_attacked\" delta=\"100\" />\n";
        assembledType += reaction;
    }
    else
    {
        // This Shouldn't Happen Ever
        Error2("Error parsing tribe traits. Unknown trait: %s", currentTribe->aggressivity.GetData());
        exit(1);
    }

    // Unity Trait
    if(currentTribe->unity != "cowards")
    {
        // Tell tribe to help
        reaction = "<react event=\"attack\" behavior=\"united_attacked\" delta=\"100\" />";
        assembledType += reaction;
    }

    // Active period of the day trait
    if(currentTribe->sleepPeriod == "diurnal")
    {
        reaction = "<react event=\"time\" value=\"22,0,,,\" behavior=\"GoToSleep\" />\n";
        reaction += "<react event=\"time\" value=\"6,0,,,\" behavior=\"do nothing\" />";
        assembledType += reaction;
    }
    else if(currentTribe->sleepPeriod == "nocturnal")
    {
        reaction = "<react event=\"time\" value=\"8,0,,,\" behavior=\"GoToSleep\" />\n";
        reaction += "<react event=\"time\" value=\"18,0,,,\" behavior=\"do nothing\" />";
        assembledType += reaction;
    }

    assembledType.Append("</npctype>");

    // Put the new NPCType into the npcclient's hashmap
    npcclient->AddNPCType(assembledType);

    // And save it into the tribeData, just in case
    currentTribe->global = assembledType;
}

int RecipeManager::ApplyRecipe(RecipeTreeNode* bestRecipe, Tribe* tribe, int step)
{
    Recipe*                      recipe       = bestRecipe->recipe;
    csArray<Recipe::Requirement> requirements = recipe->GetRequirements();
    csStringArray                algorithm    = recipe->GetAlgorithm();
    csString                     function;
    // selectedNPCs will keep the npcs required for the recipe
    // It is used so that the ParseFunction method can fire perceptions on them
    csArray<NPC*>                selectedNPCs;
    
    // Check all requirements
    int i = (bestRecipe->requirementParseType == REQ_DISTRIBUTED) ? bestRecipe->nextReq : 0;
    for(i;i<requirements.GetSize();i++)
    {
        //TODO -- Remove printf
        /*
        printf("ZeeDebug: Parsing req(index:%d) for recipe %s of type: %d\n", 
                            i, 
                            bestRecipe->recipe->GetName().GetData(),
                            bestRecipe->requirementParseType);
        */

        if(ParseRequirement(requirements[i], tribe, recipe))
        {
            // Requirement met
            bestRecipe->nextReq = (i = requirements.GetSize() - 1) ? 0 : i + 1;
        }
        else
        {
            // Keep same step for concentrated recipes... pick next one for distributed recipes
            bestRecipe->nextReq = (bestRecipe->requirementParseType == REQ_DISTRIBUTED) ? i + 1 : i;
            // Value code for 'Requirement not met'
            return -2;
        }
    }

    // If we got here it means requirements are met

    // Execute Algorithm
    for(int i=step;i<algorithm.GetSize();i++)
    {
        function = algorithm.Get(i);
        // If algorithm step needs wait time, signal it and return the step
        if(!ParseFunction(function, tribe, selectedNPCs, recipe))
            return i+1;
    }

    // If we reached the end of the algorithm then it is completed
    return -1;
}

Recipe* RecipeManager::GetRecipe(int recipeID)
{
    for(int i=0;i<recipes.GetSize();i++)
        if(recipes[i]->GetID() == recipeID)
            return recipes[i];

    // Could not find it
    CPrintf(CON_ERROR, "Could not find recipe with id %d.\n", recipeID);
    return NULL;
}

Recipe* RecipeManager::GetRecipe(csString recipeName)
{
    for(int i=0;i<recipes.GetSize();i++)
	    if(recipes[i]->GetName() == recipeName)
	        return recipes[i];

    // Could not find it
    CPrintf(CON_ERROR, "Could not find recipe %s.\n", recipeName.GetData());
    return NULL;
}

RecipeManager::TribeData* RecipeManager::GetTribeData(Tribe *tribe)
{
    for(int i=0;i<tribeData.GetSize();i++)
        if(tribeData[i].tribeID == tribe->GetID())
            return &tribeData[i];

    return NULL;
}

void RecipeManager::DumpError(csString recipeName, csString functionName, int expectedArgs, int receivedArgs)
{
    CPrintf(CON_ERROR, "Error parsing %s. Function %s expected %d arguments and received %d.\n",
            recipeName.GetData(),
            functionName.GetData(),
            expectedArgs,
            receivedArgs);
}
