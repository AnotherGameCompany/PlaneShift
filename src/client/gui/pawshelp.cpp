/*
 * pawsSplashWindow.cpp - Author: Andrew Craig
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

#include "globals.h"

#include "pawshelp.h"
#include "paws/pawsmanager.h"
#include "../pscelclient.h"
#include "util/localization.h"
#include "util/psstring.h"
#include "gui/pawscontrolwindow.h"

pawsHelp::pawsHelp()
{
    vfs =  csQueryRegistry<iVFS > ( PawsManager::GetSingleton().GetObjectRegistry());
    xml = psengine->GetXMLParser ();
    helpDoc = xml->CreateDocument();
    csString filename(PawsManager::GetSingleton().GetLocalization()->FindLocalizedFile("data/help.xml"));
    csRef<iDataBuffer> buf (vfs->ReadFile (filename));
    if (!buf || !buf->GetSize ())
    {
        printf("ERROR: Loading 'data/help.xml'");
        return;
    }
    const char* error = helpDoc->Parse( buf );
    if ( error )
    {
        printf("ERROR: Invalid 'data/help.xml': %s\n", error);
        return ;
    }
}

bool pawsHelp::PostSetup(void)
{
    helpText = dynamic_cast<pawsMultiLineTextBox*>(FindWidget("HelpText"));
    if (!helpText)
    return false;

    // creates tree:
    helpTree = new pawsSimpleTree;
    if (helpTree == NULL)
    {
        Error1("Could not create widget pawsSimpleTree");
        return false;
    }
    AddChild(helpTree);
    helpTree->SetRelativeFrame(37,29,GetActualWidth(198),GetActualHeight(224));
    helpTree->SetNotify(this);
    helpTree->SetAttachFlags(ATTACH_TOP | ATTACH_BOTTOM | ATTACH_LEFT);
    helpTree->SetScrollBars(false, true);
    helpTree->Resize();
    helpTree->SetDefaultColor(psengine->GetG2D()->FindRGB(255,255,255));

    helpTree->InsertChildL("", "RootTopic", "", "");

    LoadHelps(helpDoc->GetRoot()->GetNode("help"), "RootTopic");

    pawsTreeNode * child = helpTree->GetRoot()->GetFirstChild();
    while (child != NULL)
    {
        child->CollapseAll();
        child = child->GetNextSibling();
    }

    return true;
}

void pawsHelp::LoadHelps(iDocumentNode *node, csString parent)
{
    csRef<iDocumentNodeIterator> helpIter = node->GetNodes();
    while(helpIter->HasNext())
    {
        csRef<iDocumentNode> topicName = helpIter->Next();
        csRef<iDocumentAttribute> level = topicName->GetAttribute("level");
        if (level)
        {
            int type = psengine->GetCelClient()->GetMainPlayer()->GetType();
            if (type < level->GetValueAsInt())
                continue;
        }
        if (!strcmp(topicName->GetValue(), "branch"))
            helpTree->InsertChildL(parent,  topicName->GetAttributeValue("name"), topicName->GetAttributeValue("name"), "");
        else if (!strcmp(topicName->GetValue(), "topic"))
        {
            helpTree->InsertChildL(parent,  topicName->GetAttributeValue("name"), topicName->GetAttributeValue("name"), "");
            continue;
        }
        else
            continue;
        // Load children
        LoadHelps(topicName, topicName->GetAttributeValue("name"));
    }
}

bool pawsHelp::OnSelected(pawsWidget *widget)
{
    pawsTreeNode* node = static_cast<pawsTreeNode*> (widget);
    csRef<iDocumentNode> root = helpDoc->GetRoot()->GetNode("help");
    if(!root)
    {
        Error1("No <help> tag!");
        return false;
    }

    // xmlnode contains the root
    root->IncRef();
    csRef<iDocumentNode> xmlnode = RetrieveHelp(node,root);
    if (!xmlnode)
        return false;

    csString text(xmlnode->GetContentsValue());
    if (text)
        helpText->SetText(text.GetData());

    return true;
}

iDocumentNode* pawsHelp::RetrieveHelp(pawsTreeNode* node, iDocumentNode* helpRoot)
{
    if (!strcmp(node->GetName(),"RootTopic"))
        return helpRoot;
    csRef<iDocumentNode> parentnode = RetrieveHelp(node->GetParent(),helpRoot);
    if (!parentnode)
    {
        Error2("Can not find '%s' in help tree!", node->GetName());
        return NULL;
    }
    parentnode->DecRef();
    csRef<iDocumentNodeIterator> iter = parentnode->GetNodes();
    while (iter->HasNext())
    {
        csRef<iDocumentNode> xmlnode = iter->Next();

        // it is possible to have an empty attribute
        // (no names or values) if text is in the body of the branch
        if (xmlnode->GetAttributeValue("name"))
        {
            if (!strcmp(node->GetName(),xmlnode->GetAttributeValue("name")))
            {
                xmlnode->IncRef();
                return xmlnode;
            }
        }
    }
    return NULL;
}

pawsHelp::~pawsHelp()
{
}
