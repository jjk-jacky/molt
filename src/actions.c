/**
 * molt - Copyright (C) 2012 Olivier Brunel
 *
 * actions.c
 * Copyright (C) 2012 Olivier Brunel <i.am.jack.mail@gmail.com>
 * 
 * This file is part of molt.
 *
 * molt is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * molt is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * molt. If not, see http://www.gnu.org/licenses/
 */

/* glib */
#include <glib-2.0/glib.h>

/* molt */
#include "molt.h"
#include "internal.h"

extern GHashTable *actions;
extern GHashTable *new_names;
extern gint        nb_conflicts;
extern gint        nb_two_steps;

static void     old_name_not_available (action_t *action);
static gboolean ensure_new_name_free (action_t *action);
static void     set_conflict_FS (action_t *action);
static gboolean resolve_conflict_FS (action_t *action, action_t *action_pending,
                                     action_t *action_for);


static void
old_name_not_available (action_t *action)
{
    action_t *a;
    
    /* any action that wanted to take this name? if so, since it won't
     * be released anymore, it must be set in conflict-FS, unless
     * it's in conflict already.
     * Note: This is called when an action will not be renamed anymore, i.e.
     * from being marked conflict or conflict-FS. Which means we know that any
     * action that wanted to get this name was either going to (be renamed), or
     * in conflict, but couldn't already be in conflict-FS */
    a = g_hash_table_lookup (new_names, (gpointer) action->file);
    if (a && !(a->state & ST_CONFLICT))
    {
        debug (LEVEL_VERBOSE, "action for %s wanted to take that name (%s), "
                "setting it to conflict-FS\n",
                a->file, action->file);
        if (a->state & ST_TWO_STEPS)
        {
            --nb_two_steps;
        }
        a->state &= ~(ST_TO_RENAME | ST_TWO_STEPS);
        set_conflict_FS (a);
    }
    else
    {
        debug (LEVEL_VERBOSE, "no action wants that name\n");
    }
}

static gboolean
ensure_new_name_free (action_t *action)
{
    action_t *a;
    
    /* is there already an action for this new name? */
    a = g_hash_table_lookup (new_names, (gpointer) action->new_name);
    if (a && a != action)
    {
        debug (LEVEL_DEBUG, "new name (%s) already reserved, setting up conflict\n",
               action->new_name);
        
        /* mark action in conflict */
        action->state |= ST_CONFLICT;
        ++nb_conflicts;
        
        /* is found action already in conflict? */
        if (!(a->state & ST_CONFLICT))
        {
            /* nope, so we need to set it in conflict as well */
            debug (LEVEL_VERBOSE, "also marking conflict for action for %s\n",
                   a->file);
            a->state |= ST_CONFLICT;
            if (!(a->state & ST_CONFLICT_FS))
            {
                ++nb_conflicts;
            }
            else if (a->state & ST_TWO_STEPS)
            {
                --nb_two_steps;
            }
            /* since in conflict, it cannot also be in conflict-FS or to-rename */
            a->state &= ~(ST_CONFLICT_FS | ST_TO_RENAME | ST_TWO_STEPS);
            /* Note: if an action is/should be both in conflict & conflict-FS,
             * we only keep conflict. Because that's all we need to know, since
             * we do not (try to) rename when in conflict. (And conflict is
             * permanent, once there's one set it can't be resolved.) */
            
            /* old name is not available (anymore), make sure it's dealt with */
            old_name_not_available (a);
        }
        
        /* new-name not free */
        return FALSE;
    }
    else
    {
        debug (LEVEL_DEBUG, "new name (%s) is free\n", action->new_name);
        /* new-name is free (can be marked to-rename/conflict-FS) */
        return TRUE;
    }
}

static void
set_conflict_FS (action_t *action)
{
    debug (LEVEL_DEBUG, "-> set_conflict_FS (%s)\n", action->file);
    
    /* make sure the new name is not already taken (i.e. that another action
     * isn't already trying to get it -- this does NOT check if an action
     * owns the name) */
    if (!ensure_new_name_free (action))
    {
        /* action was marked conflict, we're done here */
        debug (LEVEL_VERBOSE, "<- set_conflict_FS (%s)\n", action->file);
        return;
    }
    
    /* mark conflict-FS */
    action->state |= ST_CONFLICT_FS;
    ++nb_conflicts;
    
    /* since no action wants that name (else ensure_new_name_free would have
     * failed) we need to take it - that way we'll get updated if the
     * conflict-FS gets resolved, or if another action also wants that name */
    debug (LEVEL_VERBOSE, "adding action to list of new names\n");
    g_hash_table_insert (new_names, (gpointer) action->new_name,
                         (gpointer) action);
    
    /* old name is not available (anymore), make sure it's dealt with */
    old_name_not_available (action);
    
    debug (LEVEL_VERBOSE, "<- set_conflict_FS (%s)\n", action->file);
}

static gboolean
resolve_conflict_FS (action_t *action, action_t *action_pending, action_t *action_for)
{
    action_t *a;
    
    debug (LEVEL_DEBUG, "-> resolve_conflict_FS (%s, pending: %s, for: %s)\n",
           action->file, action_pending->file, action_for->file);
    
    /* does the action want the current name of action_pending? */
    if (g_strcmp0 (action->new_name, action_pending->file) == 0)
    {
        /* yes! cool, since action_pending wants to free it, conflict resolved! */
        debug (LEVEL_DEBUG, "match! conflict-FS resolved, marking to-rename\n");
        action->state &= ~ST_CONFLICT_FS;
        --nb_conflicts;
        set_to_rename (action, action_for);
        debug (LEVEL_VERBOSE, "<- resolve_conflict_FS (%s, pending: %s, for: %s)\n",
               action->file, action_pending->file, action_for->file);
        return TRUE;
    }
    
    /* check if the new name is already taken by a file we'll process */
    a = g_hash_table_lookup (actions, (gpointer) action->new_name);
    if (a)
    {
        debug (LEVEL_VERBOSE, "an action owns the action's new name!\n");
        
        if (a->state & ST_TO_RENAME)
        {
            debug (LEVEL_VERBOSE, "said action will free the name, ");
            debug (LEVEL_DEBUG, "conflict-FS resolved, marking to-rename\n");
            set_to_rename (action, action_for);
            debug (LEVEL_VERBOSE, "<- resolve_conflict_FS (%s, pending: %s, for: %s)\n",
                   action->file, action_pending->file, action_for->file);
            return TRUE;
        }
        else if (a->state & ST_CONFLICT_FS)
        {
            debug (LEVEL_VERBOSE, "said action is in conflict FS, try resolving it\n");
            if (resolve_conflict_FS (a, action_pending, action_for)
                && a->state & ST_TO_RENAME)
            {
                /* action might have been set to rename already during conflict
                 * resolution */
                if (!(action->state & ST_TO_RENAME))
                {
                    debug (LEVEL_DEBUG, "conflict-FS resolved, marking to-rename\n");
                    set_to_rename (action, action_for);
                }
                debug (LEVEL_VERBOSE, "<- resolve_conflict_FS (%s, pending: %s, for: %s)\n",
                       action->file, action_pending->file, action_for->file);
                return TRUE;
            }
        }
    }
    else
    {
        debug (LEVEL_VERBOSE, "no action owns the new name\n");
    }
    
    debug (LEVEL_DEBUG, "could not resolve conflict-FS\n");
    debug (LEVEL_VERBOSE, "<- resolve_conflict_FS (%s, pending: %s, for: %s)\n",
           action->file, action_pending->file, action_for->file);
    return FALSE;
}

void
set_to_rename (action_t *action, action_t *action_for)
{
    action_t *a;
    
    debug (LEVEL_DEBUG, "-> set_to_rename (%s, for: %s)\n",
           action->file,
           action_for->file);
    
    /* make sure the new name is not already taken (i.e. that another action
     * isn't already trying to get it -- this does NOT check if an action
     * owns the name) */
    if (!ensure_new_name_free (action))
    {
        /* action was marked conflict, we're done here */
        debug (LEVEL_VERBOSE, "<- set_to_rename (%s, for: %s)\n",
               action->file,
               action_for->file);
        return;
    }
    
    /* if we're doing this fo ran action owning our new name, it'll be free */
    if (g_strcmp0 (action->new_name, action_for->file) == 0)
    {
        debug (LEVEL_VERBOSE, "action will take name of action_for\n");
    }
    else
    {
        debug (LEVEL_VERBOSE, "check if an action owns the new name (%s)\n",
               action->new_name);
        /* is there already an action that owns the new name? */
        a = g_hash_table_lookup (actions, (gpointer) action->new_name);
        if (a)
        {
            debug (LEVEL_VERBOSE, "an action owns our new name!\n");

            if (a != action_for)
            {
                /* is it gonna be renamed? */
                if (a->state & ST_TO_RENAME)
                {
                    /* then all we need here is a two-step rename to avoid conflict */
                    debug (LEVEL_VERBOSE, "said action will free the name\n");
                }
                /* is it a file system conflict? (i.e. its new name is taken) */
                else if (a->state & ST_CONFLICT_FS)
                {
                    debug (LEVEL_VERBOSE, "said action is in conflict FS, "
                        "try resolving it...\n");
                    /* we might resolve the conflict if an action wants our name and
                    * couldn't since we have it. */
                    if (resolve_conflict_FS (a, action, action_for)
                        && a->state & ST_TO_RENAME)
                    {
                        /* conflict FS was resolved, and rename will now take place,
                        * so we can do our rename, but as a two-steps process to
                        * avoid name conflict.
                        * we need to check the action's (new) state because resolving
                        * the conflict FS just means something was changed, but
                        * the action could still not lead to a rename.
                        * IOW if resolve_conflict_FS returned TRUE we know that
                        * set_to_rename was called on the action (a), but that
                        * can lead to conflict[_FS] just as well as a rename... */
                        debug (LEVEL_VERBOSE, "...resolved!\n");
                    }
                    else
                    {
                        /* couldn't resolve conflict FS for a, so we're in the
                        * same situation with it ourself */
                        debug (LEVEL_VERBOSE, "...failed; marking conflict-FS\n");
                        set_conflict_FS (action);
                        debug (LEVEL_VERBOSE, "<- set_to_rename (%s, for: %s)\n",
                               action->file,
                               action_for->file);
                        return;
                    }
                }
                /* then the file can't (in conflict) or won't be renamed */
                else
                {
                    debug (LEVEL_VERBOSE, "said action can't/won't be renamed; "
                        "marking conflict-FS\n");
                    set_conflict_FS (action);
                    debug (LEVEL_VERBOSE, "<- set_to_rename (%s, for: %s)\n",
                           action->file,
                           action_for->file);
                    return;
                }
            }
            else
            {
                debug (LEVEL_VERBOSE, "it's the action we're looping for\n");
            }
        }
        else
        {
            /* if not, check the file system (if so, we assume things have
            * been dealt with before calling set_to_rename) */
            debug (LEVEL_VERBOSE, "no action owns the new name, checking FS\n");
            if (g_file_test (action->new_name, G_FILE_TEST_EXISTS))
            {
                debug (LEVEL_DEBUG, "file exists already, marking conflict-FS\n");
                action->state |= ST_CONFLICT_FS;
                ++nb_conflicts;
            }
        }
    }
    
    /* since no action wants that name (else ensure_new_name_free would have
     * failed) we need to take it - that way we'll get updated if to conflict
     * if another action also wants that name */
    debug (LEVEL_VERBOSE, "adding action to list of new names\n");
    g_hash_table_insert (new_names, (gpointer) action->new_name,
                         (gpointer) action);
    
    /* unless we marked conflict-FS, we mark to-rename */
    if (!(action->state & ST_CONFLICT_FS))
    {
        /* if an action owns our new name, and will be renamed AFTER us */
        if (a && a->cur > action->cur)
        {
            debug (LEVEL_DEBUG, "marking two-steps\n");
            action->state |= ST_TWO_STEPS;
            ++nb_two_steps;
        }
        else
        {
            debug (LEVEL_DEBUG, "marking one-step\n");
        }
        action->state |= ST_TO_RENAME;
        
        /* is there an action that wants our old name? */
        a = g_hash_table_lookup (new_names, (gpointer) action->file);
        if (a && a->state & ST_CONFLICT_FS)
        {
            debug (LEVEL_DEBUG, "unmarking conflict-FS for action for %s\n",
                a->file);
            a->state &= ~ST_CONFLICT_FS;
            --nb_conflicts;
            set_to_rename (a, a);
        }
    }
    
    debug (LEVEL_VERBOSE, "<- set_to_rename (%s, for: %s)\n",
           action->file,
           action_for->file);
}
