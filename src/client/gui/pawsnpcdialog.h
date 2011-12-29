/*
 * Author: Christian Svensson
 *
 * Copyright (C) 2008 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
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
#ifndef PAWS_NPC_DIALOG
#define PAWS_NPC_DIALOG

#include "net/subscriber.h"
#include "pscelclient.h"

#include "paws/pawswidget.h"
#include "paws/pawsstringpromptwindow.h"

#define CONFIG_NPCDIALOG_FILE_NAME       "/planeshift/userdata/options/npcdialog.xml"
#define CONFIG_NPCDIALOG_FILE_NAME_DEF   "/planeshift/data/options/npcdialog_def.xml"


class pawsListBox;

/** This window shows the popup menu of available responses
 *  when talking to an NPC.
 */
class pawsNpcDialogWindow: public pawsWidget, public psClientNetSubscriber, public iOnStringEnteredAction
{
public:
    struct QuestInfo
    {
        csString    title;
        csString    text;
        csString    trig;
    };
    pawsNpcDialogWindow();

    bool PostSetup();
    void HandleMessage( MsgEntry* me );

    void OnListAction( pawsListBox* widget, int status );

    void OnStringEntered(const char *name,int param,const char *value);

    /**
     * Loads the settings from the config files and sets the window
     * appropriately.
     * @return TRUE if loading succeded
     */
    bool LoadSetting();

    /**
     * Shows the window and applies some special handling to fix up the window
     * Behaviour and graphics correctly depending if we use the classic menu
     * or the bubble menu
     */
    virtual void Show();
    bool OnKeyDown(utf32_char keyCode, utf32_char key, int modifiers );
    bool OnMouseDown( int button, int modifiers, int x , int y );
    bool OnButtonPressed( int button, int keyModifier, pawsWidget* widget );


    /**
     * @brief Load quest info from xmlbinding message
     *
     * @param xmlbinding An xml string which contains quest info
     *
     */
    void LoadQuest(csString xmlstr);

    /**
     * @brief Display quests in bubbles.
     *
     * @param index From which index in questInfo array the quest info will be displayed in bubbles.
     *
     */
    void DisplayQuest(unsigned int index);

    /**
     * @brief Display NPC's chat text
     *
     * @param lines Content that the NPC's chat text
     * @param npcname The target NPC name
     */
    void NpcSays(csArray<csString>& lines, GEMClientActor *actor);

    /**
     * Handles timing to make bubbles disappear in the bubbles npc dialog mode.
     */
    virtual void Draw();

    /**
     * Has a real functionality only when using the bubbles npc dialog.
     * It will avoid drawing the background in order to make it transparent
     */
    virtual void DrawBackground();

    /**
     * Getter for useBubbles, which states if we use the bubbles based
     * npcdialog interface.
     * @return TRUE if we are using the bubbles based npc dialog interface
     *         FALSE if we are using the menu based npc dialog interface
     */
    bool GetUseBubbles() { return useBubbles; }
    /**
     * Sets if we have to use the bubbles based npc dialog interface (true)
     * or the classic menu based one (false).
     * @note This doesn't reconfigure the widgets for the new modality.
     */
    void SetUseBubbles(bool useBubblesNew) { useBubbles = useBubblesNew; }


    /**
     * Saves the setting of the menu used for later use
     */
    void SaveSetting();

    /**
     * Sets the widget appropriate for display depending on the type of
     * npc dialog menu used
     */
    void SetupWindowWidgets();


private:
    void AdjustForPromptWindow();
    /**
     * Handles the inner display of text bubbles from the player
     */
    void DisplayTextBubbles(const char *sayWhat);
    bool useBubbles; ///< Stores which modality should be used for the npcdialog (bubbles/menus)

    csArray<QuestInfo> questInfo;///< Stores all the quest info and triggers parsed from xml binding.
    unsigned int    displayIndex;///< Index to display which quests
    int             cameraMode;///< Stores the camera mode

    pawsListBox* responseList;
    csTicks         ticks;
};


CREATE_PAWS_FACTORY( pawsNpcDialogWindow );
#endif