/*
 * inventorywindow.cpp - Author: Andrew Craig
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
#include <csutil/util.h>
#include <csutil/xmltiny.h>
#include <iutil/evdefs.h>
#include <imesh/spritecal3d.h>

#include "net/message.h"
#include "net/msghandler.h"
#include "net/cmdhandler.h"
#include "util/strutil.h"
#include "util/psconst.h"
#include "globals.h"

#include "pscelclient.h"



#include "paws/pawstextbox.h"
#include "paws/pawsprefmanager.h"
#include "inventorywindow.h"
#include "paws/pawsmanager.h"
#include "paws/pawsbutton.h"
#include "paws/pawstexturemanager.h"
#include "paws/pawslistbox.h"
#include "paws/pawsnumberpromptwindow.h"

#include "gui/pawsmoney.h"
#include "gui/pawsexchangewindow.h"
#include "gui/pawsslot.h"
#include "gui/pawscontrolwindow.h"
#include "gui/pawsinventorydollview.h"
#include "../charapp.h"

#define VIEW_BUTTON 1005
#define QUIT_BUTTON 1006

#define DEC_STACK_BUTTON 1010
#define INC_STACK_BUTTON 1011

#define  PSCHARACTER_SLOT_COUNT 16

/**********************************************************************
*
*                        class pawsInventoryWindow
*
***********************************************************************/

pawsInventoryWindow::pawsInventoryWindow()
{
    msgHandler = NULL;

    loader =  csQueryRegistry<iLoader > ( PawsManager::GetSingleton().GetObjectRegistry() );
    
    bulkSlots.SetSize( 32 );
    equipmentSlots.SetSize( PSCHARACTER_SLOT_COUNT );
    for ( size_t n = 0; n < equipmentSlots.GetSize(); n++ )
        equipmentSlots[n] = NULL;
        
    charApp = new psCharAppearance(PawsManager::GetSingleton().GetObjectRegistry());        
}

pawsInventoryWindow::~pawsInventoryWindow()
{
    delete charApp;
}


void pawsInventoryWindow::Show()
{
    pawsControlledWindow::Show();

    // Ask the server to send us the inventory
    if ( !inventoryCache->GetInventory())
        inventoryCache->SetCacheStatus(psCache::INVALID);
}

bool pawsInventoryWindow::SetupSlot( const char* slotName )
{
    pawsSlot* slot = dynamic_cast <pawsSlot*> (FindWidget(slotName));
    if (slot == NULL)
    {
        Error2("Could not locate pawsSlot %s.",slotName);
        return false;
    }
    uintptr_t slotID = psengine->slotName.GetID(slotName);
    if ( slotID == csInvalidStringID )
    {
        Error2("Could not located the %s slot", slotName); 
        return false;
    }
    slot->SetContainer(  CONTAINER_INVENTORY_EQUIPMENT );
    slot->SetSlotID( slotID );
    slot->DrawStackCount(true);
   
    equipmentSlots[slotID] = slot;
    return true;
}

bool pawsInventoryWindow::PostSetup()
{    
    //printf("Inventory setup\n");
    msgHandler = psengine->GetMsgHandler();
    if ( !msgHandler )
        return false;
    
    // Setup the Doll
    if ( !SetupDoll() )
        return false;

    trias  = dynamic_cast <pawsTextBox*> (FindWidget("TotalTrias")); 
    if ( !trias )
        return false;
    weight = dynamic_cast <pawsTextBox*> (FindWidget("TotalWeight")); 
    if ( !weight )
        return false;
        
    money = dynamic_cast <pawsMoney*> (FindWidget("Money")); 
    if ( !money )
        return false;
    
    money->SetContainer( CONTAINER_INVENTORY_MONEY );            
     
    // If you add something here, DO NOT FORGET TO CHANGE 'INVENTORY_EQUIP_COUNT'!!!
    if ( !SetupSlot("lefthand") )    return false;
    if ( !SetupSlot("righthand") )   return false;
    if ( !SetupSlot("leftfinger") )  return false;
    if ( !SetupSlot("rightfinger") ) return false;
    if ( !SetupSlot("head") )        return false;
    if ( !SetupSlot("neck") )        return false;
    if ( !SetupSlot("back") )        return false;
    if ( !SetupSlot("arms") )        return false;    
    if ( !SetupSlot("gloves") )      return false;        
    if ( !SetupSlot("boots") )       return false;            
    if ( !SetupSlot("legs") )        return false;            
    if ( !SetupSlot("belt") )        return false;                
    if ( !SetupSlot("bracers") )     return false;            
    if ( !SetupSlot("torso") )       return false;                
    if ( !SetupSlot("mind") )        return false;
        
    pawsListBox * bulkList = dynamic_cast <pawsListBox*> (FindWidget("BulkList"));        
    for (int i = 0; i < INVENTORY_BULK_COUNT/2; i++)
    {
        pawsListBoxRow * listRow = bulkList->NewRow(i);
        for (int j = 0; j < 2; j++)
        {
            pawsSlot * slot;
            slot = dynamic_cast <pawsSlot*> (listRow->GetColumn(j));
            slot->SetContainer( CONTAINER_INVENTORY_BULK );
            //csString name;
            slot->SetSlotID( i*2+j );     
            csString name;
            name.Format("invslot_%d", 16 + i*2+j);  // 16 equip slots come first
            slot->SetSlotName(name);

            //printf("Subscribing bulk slot to %s.\n",name.GetData() );
            PawsManager::GetSingleton().Subscribe( name, slot );
            PawsManager::GetSingleton().Subscribe("sigClearInventorySlots", slot);
            bulkSlots[i*2+j] = slot;            
        }        
    }

    // Ask the server to send us the inventory
    inventoryCache = psengine->GetInventoryCache();
    if (!inventoryCache)
        return false;

    if ( !inventoryCache->GetInventory() )
    {
        inventoryCache->SetCacheStatus(psCache::INVALID);
        return false;
    }

    return true;
}

bool pawsInventoryWindow::SetupDoll()
{
    pawsObjectView* widget = dynamic_cast<pawsObjectView*>(FindWidget("InventoryDoll"));
    GEMClientActor* actor = psengine->GetCelClient()->GetMainPlayer();
    if (!widget || !actor)
        return false;

    iMeshWrapper* mesh = actor->Mesh();
    if (!mesh) 
    {
        return false;
    }        

    // Set the doll view
    widget->View( mesh );
    
    // Register this doll for updates
    widget->SetID( actor->GetEntity()->GetID() );

    csRef<iSpriteCal3DState> spstate = scfQueryInterface<iSpriteCal3DState> (widget->GetObject()->GetMeshObject());
    if (spstate)
    {
        // Setup cal3d to select random 0 velocity anims
        spstate->SetVelocity(0.0,&psengine->GetRandomGen());
    }

    charApp->Clone(actor->charApp);
    charApp->SetMesh(widget->GetObject());
    
    //printf("Inventory Applying Traits: %s\n", actor->traits.GetData());
    //printf("Inventory Applying Equipment: %s\n", actor->equipment.GetData());
    
    
    charApp->ApplyTraits(actor->traits);
    charApp->ApplyEquipment(actor->equipment);
    
    // Build doll appearance and equipment
    //bool a = psengine->BuildAppearance( widget->GetObject(), actor->traits );
    //bool e = psengine->BuildEquipment( widget->GetObject(), actor->equipment, actor->traitList );
    
    return true;
    //return (a && e);
}

bool pawsInventoryWindow::OnMouseDown( int button, int keyModifier, int x, int y )
{
    // Check to see if we are dropping an item
    if ( psengine->GetSlotManager() && psengine->GetSlotManager()->IsDragging() )
    {
        psengine->GetSlotManager()->CancelDrag();
        return true;
    }
    return pawsControlledWindow::OnMouseDown(  button, keyModifier, x, y );
}


bool pawsInventoryWindow::OnButtonPressed( int mouseButton, int keyModifer, pawsWidget* widget )
{
    // Check to see if this was the view button.
    if ( widget->GetID() == VIEW_BUTTON )
    {       
        if ( psengine->GetSlotManager()->IsDragging() )
        {
            psViewItemDescription out(psengine->GetSlotManager()->HoldingContainerID(), 
                                      psengine->GetSlotManager()->HoldingSlotID());   
            msgHandler->SendMessage( out.msg );

            psengine->GetSlotManager()->CancelDrag();
        }
    
        return true;
    }

    return true;
}

void pawsInventoryWindow::Close()
{
    Hide();
}

pawsSlot* pawsInventoryWindow::GetFreeSlot()
{
    for ( size_t n = 0; n < bulkSlots.GetSize(); n++ )
    {    
        if ( bulkSlots[n] && bulkSlots[n]->IsEmpty() )
        {
            return bulkSlots[n];
        }       
    }

    return NULL;
}


void pawsInventoryWindow::Dequip( const char* itemName )
{    
    if ( itemName != NULL )
    {                    
        pawsSlot* fromSlot = NULL;
        // See if we can find the item in the equipment slots.
        for ( size_t z = 0; z < equipmentSlots.GetSize(); z++ )
        {                   
            if ( equipmentSlots[z] && !equipmentSlots[z]->IsEmpty() )
            {                               
                csString tip(equipmentSlots[z]->GetToolTip()); 
                if ( tip.CompareNoCase(itemName) )                
                {
                    fromSlot = equipmentSlots[z];
                    break;
                }                
            }
        }
         
        if ( fromSlot == NULL ) // if item was not found, look in slotnames 
            fromSlot = dynamic_cast <pawsSlot*> (FindWidget(itemName));    
    
        if ( fromSlot )
        {
            int container   = fromSlot->ContainerID();
            int slot        = fromSlot->ID();
            int stackCount  = fromSlot->StackCount();
                   
            pawsSlot* freeSlot = GetFreeSlot();
            if ( freeSlot )
            {
            
                // Move from the equipped slot to an empty slot
                psSlotMovementMsg msg( container, slot,
                                       freeSlot->ContainerID() ,
                                       freeSlot->ID(),
                                       stackCount );
                               
                msgHandler->SendMessage( msg.msg );                                               
                fromSlot->Clear();
            }
            
        }    
    }        
}

void pawsInventoryWindow::Equip( const char* itemName, int stackCount )
{    
    if ( itemName != NULL )
    {        
        pawsSlot* fromSlot = NULL;        
        for ( size_t z = 0; z < bulkSlots.GetSize(); z++ )
        {
            if ( !bulkSlots[z]->IsEmpty() )
            {
                csString tip(bulkSlots[z]->GetToolTip()); 
                if ( tip.CompareNoCase(itemName) )
                {
                    fromSlot = bulkSlots[z];
                    break;
                }                
            }
        }
            
        if ( fromSlot )
        {
            int container   = fromSlot->ContainerID();
            int slot        = fromSlot->ID();
        
            //psItem* item = charData->GetItemInSlot( slot );    
            csRef<MsgHandler> msgHandler = psengine->GetMsgHandler();
            psSlotMovementMsg msg( container, slot,
                               CONTAINER_INVENTORY_EQUIPMENT, -1,
                               stackCount );
            msgHandler->SendMessage( msg.msg );                                               
        }
    }         
}

//search for items in bulk, then in equipped slots
void pawsInventoryWindow::Write( const char* itemName )
{
    if ( itemName != NULL )
    {        
        pawsSlot* fromSlot = NULL;        
        for ( size_t z = 0; z < bulkSlots.GetSize(); z++ )
        {
            if ( !bulkSlots[z]->IsEmpty() )
            {
                csString tip(bulkSlots[z]->GetToolTip()); 
                if ( tip.CompareNoCase(itemName) )
                {
                    fromSlot = bulkSlots[z];
                    break;
                }                
            }
        }
        
        if( fromSlot == NULL){
            // See if we can find the item in the equipment slots.
            for ( size_t z = 0; z < equipmentSlots.GetSize(); z++ )
            {                   
                if ( equipmentSlots[z] && !equipmentSlots[z]->IsEmpty() )
                {                               
                    csString tip(equipmentSlots[z]->GetToolTip()); 
                    if ( tip.CompareNoCase(itemName) )                
                    {
                        fromSlot = equipmentSlots[z];
                        break;
                    }                
                }
            }
             
            if ( fromSlot == NULL ) // if item was not found, look in slotnames 
                fromSlot = dynamic_cast <pawsSlot*> (FindWidget(itemName));    
        }
            
        if ( fromSlot )
        {
           printf("Found item %s to write on\n", itemName);
            int container   = fromSlot->ContainerID();
            int slot        = fromSlot->ID();
        
            //psItem* item = charData->GetItemInSlot( slot );    
            csRef<MsgHandler> msgHandler = psengine->GetMsgHandler();
            psWriteBookMessage msg(slot, container);
            msgHandler->SendMessage( msg.msg );                                               
        }
    }         

}
