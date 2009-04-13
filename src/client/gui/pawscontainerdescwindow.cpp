/*
 * pawscontainerdescriptionwidow.cpp - Author: Thomas Towey
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

// CS INCLUDES
#include <csgeom/vector3.h>
#include <iutil/objreg.h>


// CLIENT INCLUDES
#include "pscelclient.h"

// PAWS INCLUDES
#include "pawscontainerdescwindow.h"
#include "paws/pawstextbox.h"
#include "paws/pawslistbox.h"
#include "inventorywindow.h"
#include "paws/pawsmanager.h"
#include "net/messages.h"
#include "net/clientmsghandler.h"
#include "net/cmdhandler.h"

#include "util/log.h"
#include "gui/pawsslot.h"

#include "globals.h"


// BUTTONS AND SLOTS
#define VIEW_BUTTON 11
#define INVENTORY_BUTTON 12
#define COMBINE_BUTTON 13

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

pawsContainerDescWindow::pawsContainerDescWindow()
{
    containerID = 0;
    containerSlots = 0;
}

pawsContainerDescWindow::~pawsContainerDescWindow()
{
}

bool pawsContainerDescWindow::PostSetup()
{
    msgHandler = psengine->GetMsgHandler();

    if ( !msgHandler ) return false;
    if ( !msgHandler->Subscribe(this, MSGTYPE_VIEW_CONTAINER ) )
        return false;        
    if ( !msgHandler->Subscribe(this, MSGTYPE_UPDATE_ITEM ) )
        return false;        

    // Store some of our children for easy access later on.
    name = (pawsTextBox*)FindWidget("ItemName");
    if (!name)
        return false;

    description = dynamic_cast<pawsMultiLineTextBox*> (FindWidget("ItemDescription"));
    if (!description)
        return false;
    
    pic = (pawsWidget*)FindWidget("ItemImage");
    if (!pic)
        return false;
    
    // Create bulk slots.
    contents = dynamic_cast <pawsListBox*> (FindWidget("BulkList"));
    if (!contents)
        return false;

    return true;
}

void pawsContainerDescWindow::HandleUpdateItem( MsgEntry* me )
{
    psViewItemUpdate mesg( me );
    csString sigData, data;

    // We send ownerID to multiple clients, so each client must decide if the item is owned by
    // them or not.  This is double checked on the server if someone tries to move an item,
    // so hacking this to override just breaks the display, but does not enable a cheat.
    if (mesg.ownerID.IsValid() && mesg.ownerID != psengine->GetCelClient()->GetMainPlayer()->GetEID())
    {
        mesg.stackCount = -1; // hardcoded signal that item is not owned by this player
    }

    sigData.Format("invslot_%d", mesg.containerID.Unbox() * 100 + mesg.slotID + 16);
    if (!mesg.clearSlot)
    {
        data.Format("%s %d %d %s", mesg.icon.GetData(), mesg.stackCount, 0, mesg.name.GetData());
    }

    printf("Got item update for %s: %s\n", sigData.GetDataSafe(), data.GetDataSafe() );

    // FIXME: psViewItemMessages should probably send out purification status

    PawsManager::GetSingleton().Publish(sigData, data);
}

void pawsContainerDescWindow::HandleViewItem( MsgEntry* me )
{
    Show();
    psViewItemDescription mesg( me );
    
    description->SetText( mesg.itemDescription );
    name->SetText( mesg.itemName );       
    pic->Show();
    pic->SetBackground(mesg.itemIcon);
    if (pic->GetBackground() != mesg.itemIcon) // if setting the background failed...hide it
        pic->Hide();

    bool newContainer = false;
    if (containerID != mesg.containerID)
        newContainer = true;

    containerID = mesg.containerID;

    if ( mesg.hasContents )
    {
        if (newContainer)
        {
            Debug2(LOG_CHARACTER, 0, "Setting up container %d.\n", mesg.containerID);

            contents->Clear();
            containerSlots = PSITEM_MAX_CONTAINER_SLOTS;

            const int cols = contents->GetTotalColumns(); //6;
            const int rows = (int) ceil(float(containerSlots)/cols);
            for (int i = 0; i < rows; i++)
            {
                pawsListBoxRow* listRow = contents->NewRow(i);
                for (int j = 0; j < cols; j++)
                {
                    pawsSlot* slot = dynamic_cast <pawsSlot*> (listRow->GetColumn(j));
                    CS_ASSERT( slot );

                    if (i * cols + j >= containerSlots)
                    {
                        slot->Hide();
                        continue;
                    }

                    slot->SetContainer(mesg.containerID);
                    slot->SetSlotID(i*cols+j);
                    //slot->SetDefaultToolTip("Empty");

                    csString slotName;
                    slotName.Format("invslot_%d", mesg.containerID * 100 + i*cols+j + 16); // container slot + next two digit slot number
                    slot->SetSlotName(slotName);
                    Debug3(LOG_CHARACTER, 0, "Container slot %d subscribing to %s.\n", i*cols+j, slotName.GetData());
                    // New slots must subscribe to sigClear* -before-
                    // invslot_n, or else the cached clear signal will override
                    // the signal with the cached slot data, resulting in an
                    // empty window.
                    if (containerID < 100)
                        PawsManager::GetSingleton().Subscribe("sigClearInventorySlots", slot);
                    PawsManager::GetSingleton().Subscribe("sigClearContainerSlots", slot);
                    PawsManager::GetSingleton().Subscribe(slotName, slot);
                }
            }
        }
        if (containerID > 100)
            PawsManager::GetSingleton().Publish("sigClearContainerSlots");
        for (size_t i=0; i < mesg.contents.GetSize(); i++)
        {
            csString sigData, data;
            sigData.Format("invslot_%u", mesg.containerID * 100 + mesg.contents[i].slotID + 16);

            data.Format( "%s %d %d %s", mesg.contents[i].icon.GetData(),
                mesg.contents[i].stackCount,
                mesg.contents[i].purifyStatus,
                mesg.contents[i].name.GetData() );

            printf("Publishing slot data %s -> %s\n", sigData.GetData(), data.GetData() );
            PawsManager::GetSingleton().Publish(sigData, data );
        }
        contents->Show();
    }
    else
    {
        contents->Hide();
    }
}

void pawsContainerDescWindow::HandleMessage( MsgEntry* me )
{
    switch ( me->GetType() )
    {
        case MSGTYPE_VIEW_CONTAINER:
        {
            HandleViewItem( me );
            break;
        }
        case MSGTYPE_UPDATE_ITEM:
        {
            HandleUpdateItem( me );
            break;
        }
    }
    
}

bool pawsContainerDescWindow::OnButtonPressed( int mouseButton, int keyModifier, pawsWidget* widget )
{   
    csString widgetName(widget->GetName());
    
    if ( widgetName == "SmallInvButton" )
    {
        pawsWidget* widget = PawsManager::GetSingleton().FindWidget("SmallInventoryWindow");
        if ( widget )
            widget->Show();
        return true;        
    }
    
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
    else if ( widget->GetID() == INVENTORY_BUTTON )
    {     
        if ( psengine->GetSlotManager()->IsDragging() )
        {            
            pawsInventoryWindow* inv = (pawsInventoryWindow*)PawsManager::GetSingleton().FindWidget("InventoryWindow");
            pawsSlot* slot = inv->GetFreeSlot();
        
            if(!slot)
            {
                PawsManager::GetSingleton().CreateWarningBox("Your inventory is full!");
                return true;
            }

            psengine->GetSlotManager()->Handle(slot);
        }

        return true;
    }
    else if ( widget->GetID() == COMBINE_BUTTON )
    {
        GEMClientObject* oldtarget = psengine->GetCharManager()->GetTarget();
        EID oldID;
        if(oldtarget)
        {
             oldID = oldtarget->GetEID();
        }
        //printf("selecting containerID %d, oldID %d\n", containerID, oldID);
        psUserActionMessage setnewtarget(0, containerID, "select");
        setnewtarget.SendMessage();
        //printf("combining\n");
        psengine->GetCmdHandler()->Execute("/combine");
        //printf("selecting oldID %d\n", oldID);
        psUserActionMessage setoldtarget(0, oldID, "select");
        setoldtarget.SendMessage();
    }
    return true;
}

