/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sv_edict.c -- entity dictionary

#include "quakedef.h"

dprograms_t		*progs;
mfunction_t		*pr_functions;
char			*pr_strings;
ddef_t			*pr_fielddefs;
ddef_t			*pr_globaldefs;
dstatement_t	*pr_statements;
globalvars_t	*pr_global_struct;
float			*pr_globals;			// same as pr_global_struct
int				pr_edict_size;			// in bytes
int				pr_edictareasize;		// LordHavoc: in bytes

unsigned short	pr_crc;

mempool_t		*progs_mempool;
mempool_t		*edictstring_mempool;

int		type_size[8] = {1,sizeof(string_t)/4,1,3,1,1,sizeof(func_t)/4,sizeof(void *)/4};

ddef_t *ED_FieldAtOfs(int ofs);
qboolean ED_ParseEpair(edict_t *ent, ddef_t *key, const char *s);

cvar_t	pr_checkextension = {CVAR_READONLY, "pr_checkextension", "1"};
cvar_t	nomonsters = {0, "nomonsters", "0"};
cvar_t	gamecfg = {0, "gamecfg", "0"};
cvar_t	scratch1 = {0, "scratch1", "0"};
cvar_t	scratch2 = {0,"scratch2", "0"};
cvar_t	scratch3 = {0, "scratch3", "0"};
cvar_t	scratch4 = {0, "scratch4", "0"};
cvar_t	savedgamecfg = {CVAR_SAVE, "savedgamecfg", "0"};
cvar_t	saved1 = {CVAR_SAVE, "saved1", "0"};
cvar_t	saved2 = {CVAR_SAVE, "saved2", "0"};
cvar_t	saved3 = {CVAR_SAVE, "saved3", "0"};
cvar_t	saved4 = {CVAR_SAVE, "saved4", "0"};
cvar_t	decors = {0, "decors", "0"};
cvar_t	nehx00 = {0, "nehx00", "0"};cvar_t	nehx01 = {0, "nehx01", "0"};
cvar_t	nehx02 = {0, "nehx02", "0"};cvar_t	nehx03 = {0, "nehx03", "0"};
cvar_t	nehx04 = {0, "nehx04", "0"};cvar_t	nehx05 = {0, "nehx05", "0"};
cvar_t	nehx06 = {0, "nehx06", "0"};cvar_t	nehx07 = {0, "nehx07", "0"};
cvar_t	nehx08 = {0, "nehx08", "0"};cvar_t	nehx09 = {0, "nehx09", "0"};
cvar_t	nehx10 = {0, "nehx10", "0"};cvar_t	nehx11 = {0, "nehx11", "0"};
cvar_t	nehx12 = {0, "nehx12", "0"};cvar_t	nehx13 = {0, "nehx13", "0"};
cvar_t	nehx14 = {0, "nehx14", "0"};cvar_t	nehx15 = {0, "nehx15", "0"};
cvar_t	nehx16 = {0, "nehx16", "0"};cvar_t	nehx17 = {0, "nehx17", "0"};
cvar_t	nehx18 = {0, "nehx18", "0"};cvar_t	nehx19 = {0, "nehx19", "0"};
cvar_t	cutscene = {0, "cutscene", "1"};
// LordHavoc: optional runtime bounds checking (speed drain, but worth it for security, on by default - breaks most QCCX features (used by CRMod and others))
cvar_t	pr_boundscheck = {0, "pr_boundscheck", "1"};
// LordHavoc: prints every opcode as it executes - warning: this is significant spew
cvar_t	pr_traceqc = {0, "pr_traceqc", "0"};

#define	MAX_FIELD_LEN	64
#define GEFV_CACHESIZE	2

typedef struct {
	ddef_t	*pcache;
	char	field[MAX_FIELD_LEN];
} gefv_cache;

static gefv_cache	gefvCache[GEFV_CACHESIZE] = {{NULL, ""}, {NULL, ""}};

ddef_t *ED_FindField (const char *name);
mfunction_t *ED_FindFunction (const char *name);

// LordHavoc: in an effort to eliminate time wasted on GetEdictFieldValue...  these are defined as externs in progs.h
int eval_gravity;
int eval_button3;
int eval_button4;
int eval_button5;
int eval_button6;
int eval_button7;
int eval_button8;
int eval_buttonuse;
int eval_buttonchat;
int eval_glow_size;
int eval_glow_trail;
int eval_glow_color;
int eval_items2;
int eval_scale;
int eval_alpha;
int eval_renderamt; // HalfLife support
int eval_rendermode; // HalfLife support
int eval_fullbright;
int eval_ammo_shells1;
int eval_ammo_nails1;
int eval_ammo_lava_nails;
int eval_ammo_rockets1;
int eval_ammo_multi_rockets;
int eval_ammo_cells1;
int eval_ammo_plasma;
int eval_idealpitch;
int eval_pitch_speed;
int eval_viewmodelforclient;
int eval_nodrawtoclient;
int eval_exteriormodeltoclient;
int eval_drawonlytoclient;
int eval_ping;
int eval_movement;
int eval_pmodel;
int eval_punchvector;
int eval_viewzoom;
int eval_clientcolors;
int eval_tag_entity;
int eval_tag_index;
int eval_light_lev;
int eval_color;
int eval_style;
int eval_pflags;
int eval_cursor_active;
int eval_cursor_screen;
int eval_cursor_trace_start;
int eval_cursor_trace_endpos;
int eval_cursor_trace_ent;
int eval_colormod;

mfunction_t *SV_PlayerPhysicsQC;
mfunction_t *EndFrameQC;
//KrimZon - SERVER COMMANDS IN QUAKEC
mfunction_t *SV_ParseClientCommandQC;

int FindFieldOffset(const char *field)
{
	ddef_t *d;
	d = ED_FindField(field);
	if (!d)
		return 0;
	return d->ofs*4;
}

void FindEdictFieldOffsets(void)
{
	eval_gravity = FindFieldOffset("gravity");
	eval_button3 = FindFieldOffset("button3");
	eval_button4 = FindFieldOffset("button4");
	eval_button5 = FindFieldOffset("button5");
	eval_button6 = FindFieldOffset("button6");
	eval_button7 = FindFieldOffset("button7");
	eval_button8 = FindFieldOffset("button8");
	eval_buttonuse = FindFieldOffset("buttonuse");
	eval_buttonchat = FindFieldOffset("buttonchat");
	eval_glow_size = FindFieldOffset("glow_size");
	eval_glow_trail = FindFieldOffset("glow_trail");
	eval_glow_color = FindFieldOffset("glow_color");
	eval_items2 = FindFieldOffset("items2");
	eval_scale = FindFieldOffset("scale");
	eval_alpha = FindFieldOffset("alpha");
	eval_renderamt = FindFieldOffset("renderamt"); // HalfLife support
	eval_rendermode = FindFieldOffset("rendermode"); // HalfLife support
	eval_fullbright = FindFieldOffset("fullbright");
	eval_ammo_shells1 = FindFieldOffset("ammo_shells1");
	eval_ammo_nails1 = FindFieldOffset("ammo_nails1");
	eval_ammo_lava_nails = FindFieldOffset("ammo_lava_nails");
	eval_ammo_rockets1 = FindFieldOffset("ammo_rockets1");
	eval_ammo_multi_rockets = FindFieldOffset("ammo_multi_rockets");
	eval_ammo_cells1 = FindFieldOffset("ammo_cells1");
	eval_ammo_plasma = FindFieldOffset("ammo_plasma");
	eval_idealpitch = FindFieldOffset("idealpitch");
	eval_pitch_speed = FindFieldOffset("pitch_speed");
	eval_viewmodelforclient = FindFieldOffset("viewmodelforclient");
	eval_nodrawtoclient = FindFieldOffset("nodrawtoclient");
	eval_exteriormodeltoclient = FindFieldOffset("exteriormodeltoclient");
	eval_drawonlytoclient = FindFieldOffset("drawonlytoclient");
	eval_ping = FindFieldOffset("ping");
	eval_movement = FindFieldOffset("movement");
	eval_pmodel = FindFieldOffset("pmodel");
	eval_punchvector = FindFieldOffset("punchvector");
	eval_viewzoom = FindFieldOffset("viewzoom");
	eval_clientcolors = FindFieldOffset("clientcolors");
	eval_tag_entity = FindFieldOffset("tag_entity");
	eval_tag_index = FindFieldOffset("tag_index");
	eval_light_lev = FindFieldOffset("light_lev");
	eval_color = FindFieldOffset("color");
	eval_style = FindFieldOffset("style");
	eval_pflags = FindFieldOffset("pflags");
	eval_cursor_active = FindFieldOffset("cursor_active");
	eval_cursor_screen = FindFieldOffset("cursor_screen");
	eval_cursor_trace_start = FindFieldOffset("cursor_trace_start");
	eval_cursor_trace_endpos = FindFieldOffset("cursor_trace_endpos");
	eval_cursor_trace_ent = FindFieldOffset("cursor_trace_ent");
	eval_colormod = FindFieldOffset("colormod");

	// LordHavoc: allowing QuakeC to override the player movement code
	SV_PlayerPhysicsQC = ED_FindFunction ("SV_PlayerPhysics");
	// LordHavoc: support for endframe
	EndFrameQC = ED_FindFunction ("EndFrame");
	//KrimZon - SERVER COMMANDS IN QUAKEC
	SV_ParseClientCommandQC = ED_FindFunction ("SV_ParseClientCommand");
}

/*
=================
ED_ClearEdict

Sets everything to NULL
=================
*/
void ED_ClearEdict (edict_t *e)
{
	int num;
	memset (e->v, 0, progs->entityfields * 4);
	e->e->free = false;
	// LordHavoc: for consistency set these here
	num = NUM_FOR_EDICT(e) - 1;
	if (num >= 0 && num < svs.maxclients)
	{
		eval_t *val;
		// set colormap and team on newly created player entity
		e->v->colormap = num + 1;
		e->v->team = (svs.clients[num].colors & 15) + 1;
		// set netname/clientcolors back to client values so that
		// DP_SV_CLIENTNAME and DPV_SV_CLIENTCOLORS will not immediately
		// reset them
		e->v->netname = PR_SetString(svs.clients[num].name);
		if ((val = GETEDICTFIELDVALUE(e, eval_clientcolors)))
			val->_float = svs.clients[num].colors;
		// NEXUIZ_PLAYERMODEL and NEXUIZ_PLAYERSKIN
		e->v->playermodel = PR_SetString(svs.clients[num].playermodel);
		e->v->playerskin = PR_SetString(svs.clients[num].playerskin);
	}
}

/*
=================
ED_Alloc

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t *ED_Alloc (void)
{
	int			i;
	edict_t		*e;

	for (i = svs.maxclients + 1;i < sv.num_edicts;i++)
	{
		e = EDICT_NUM(i);
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if (e->e->free && ( e->e->freetime < 2 || sv.time - e->e->freetime > 0.5 ) )
		{
			ED_ClearEdict (e);
			return e;
		}
	}

	if (i == MAX_EDICTS)
		Host_Error ("ED_Alloc: no free edicts");

	sv.num_edicts++;
	if (sv.num_edicts >= sv.max_edicts)
		SV_IncreaseEdicts();
	e = EDICT_NUM(i);
	ED_ClearEdict (e);

	return e;
}

/*
=================
ED_Free

Marks the edict as free
FIXME: walk all entities and NULL out references to this entity
=================
*/
void ED_Free (edict_t *ed)
{
	SV_UnlinkEdict (ed);		// unlink from world bsp

	ed->e->free = true;
	ed->v->model = 0;
	ed->v->takedamage = 0;
	ed->v->modelindex = 0;
	ed->v->colormap = 0;
	ed->v->skin = 0;
	ed->v->frame = 0;
	VectorClear(ed->v->origin);
	VectorClear(ed->v->angles);
	ed->v->nextthink = -1;
	ed->v->solid = 0;

	ed->e->freetime = sv.time;
}

//===========================================================================

/*
============
ED_GlobalAtOfs
============
*/
ddef_t *ED_GlobalAtOfs (int ofs)
{
	ddef_t		*def;
	int			i;

	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FieldAtOfs
============
*/
ddef_t *ED_FieldAtOfs (int ofs)
{
	ddef_t		*def;
	int			i;

	for (i=0 ; i<progs->numfielddefs ; i++)
	{
		def = &pr_fielddefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FindField
============
*/
ddef_t *ED_FindField (const char *name)
{
	ddef_t *def;
	int i;

	for (i=0 ; i<progs->numfielddefs ; i++)
	{
		def = &pr_fielddefs[i];
		if (!strcmp(PR_GetString(def->s_name), name))
			return def;
	}
	return NULL;
}

/*
============
ED_FindGlobal
============
*/
ddef_t *ED_FindGlobal (const char *name)
{
	ddef_t *def;
	int i;

	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		if (!strcmp(PR_GetString(def->s_name), name))
			return def;
	}
	return NULL;
}


/*
============
ED_FindFunction
============
*/
mfunction_t *ED_FindFunction (const char *name)
{
	mfunction_t		*func;
	int				i;

	for (i=0 ; i<progs->numfunctions ; i++)
	{
		func = &pr_functions[i];
		if (!strcmp(PR_GetString(func->s_name), name))
			return func;
	}
	return NULL;
}


/*
============
PR_ValueString

Returns a string describing *data in a type specific manner
=============
*/
//int NoCrash_NUM_FOR_EDICT(edict_t *e);
char *PR_ValueString (etype_t type, eval_t *val)
{
	static char line[1024]; // LordHavoc: enlarged a bit (was 256)
	ddef_t *def;
	mfunction_t *f;
	int n;

	type &= ~DEF_SAVEGLOBAL;

	switch (type)
	{
	case ev_string:
		strlcpy (line, PR_GetString (val->string), sizeof (line));
		break;
	case ev_entity:
		//n = NoCrash_NUM_FOR_EDICT(PROG_TO_EDICT(val->edict));
		n = val->edict;
		if (n < 0 || n >= MAX_EDICTS)
			snprintf (line, sizeof (line), "entity %i (invalid!)", n);
		else
			snprintf (line, sizeof (line), "entity %i", n);
		break;
	case ev_function:
		f = pr_functions + val->function;
		snprintf (line, sizeof (line), "%s()", PR_GetString(f->s_name));
		break;
	case ev_field:
		def = ED_FieldAtOfs ( val->_int );
		snprintf (line, sizeof (line), ".%s", PR_GetString(def->s_name));
		break;
	case ev_void:
		snprintf (line, sizeof (line), "void");
		break;
	case ev_float:
		// LordHavoc: changed from %5.1f to %10.4f
		snprintf (line, sizeof (line), "%10.4f", val->_float);
		break;
	case ev_vector:
		// LordHavoc: changed from %5.1f to %10.4f
		snprintf (line, sizeof (line), "'%10.4f %10.4f %10.4f'", val->vector[0], val->vector[1], val->vector[2]);
		break;
	case ev_pointer:
		snprintf (line, sizeof (line), "pointer");
		break;
	default:
		snprintf (line, sizeof (line), "bad type %i", type);
		break;
	}

	return line;
}

/*
============
PR_UglyValueString

Returns a string describing *data in a type specific manner
Easier to parse than PR_ValueString
=============
*/
char *PR_UglyValueString (etype_t type, eval_t *val)
{
	static char line[4096];
	int i;
	char *s;
	ddef_t *def;
	mfunction_t *f;

	type &= ~DEF_SAVEGLOBAL;

	switch (type)
	{
	case ev_string:
		// Parse the string a bit to turn special characters
		// (like newline, specifically) into escape codes,
		// this fixes saving games from various mods
		s = PR_GetString (val->string);
		for (i = 0;i < (int)sizeof(line) - 2 && *s;)
		{
			if (*s == '\n')
			{
				line[i++] = '\\';
				line[i++] = 'n';
			}
			else if (*s == '\r')
			{
				line[i++] = '\\';
				line[i++] = 'r';
			}
			else
				line[i++] = *s;
			s++;
		}
		line[i] = '\0';
		break;
	case ev_entity:
		snprintf (line, sizeof (line), "%i", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)));
		break;
	case ev_function:
		f = pr_functions + val->function;
		strlcpy (line, PR_GetString (f->s_name), sizeof (line));
		break;
	case ev_field:
		def = ED_FieldAtOfs ( val->_int );
		snprintf (line, sizeof (line), ".%s", PR_GetString(def->s_name));
		break;
	case ev_void:
		snprintf (line, sizeof (line), "void");
		break;
	case ev_float:
		snprintf (line, sizeof (line), "%f", val->_float);
		break;
	case ev_vector:
		snprintf (line, sizeof (line), "%f %f %f", val->vector[0], val->vector[1], val->vector[2]);
		break;
	default:
		snprintf (line, sizeof (line), "bad type %i", type);
		break;
	}

	return line;
}

/*
============
PR_GlobalString

Returns a string with a description and the contents of a global,
padded to 20 field width
============
*/
char *PR_GlobalString (int ofs)
{
	char	*s;
	int		i;
	ddef_t	*def;
	void	*val;
	static char	line[128];

	val = (void *)&pr_globals[ofs];
	def = ED_GlobalAtOfs(ofs);
	if (!def)
		snprintf (line, sizeof (line), "%i(?)", ofs);
	else
	{
		s = PR_ValueString (def->type, val);
		snprintf (line, sizeof (line), "%i(%s)%s", ofs, PR_GetString(def->s_name), s);
	}

	i = strlen(line);
	for ( ; i<20 ; i++)
		strlcat (line, " ", sizeof (line));
	strlcat (line, " ", sizeof (line));

	return line;
}

char *PR_GlobalStringNoContents (int ofs)
{
	int		i;
	ddef_t	*def;
	static char	line[128];

	def = ED_GlobalAtOfs(ofs);
	if (!def)
		snprintf (line, sizeof (line), "%i(?)", ofs);
	else
		snprintf (line, sizeof (line), "%i(%s)", ofs, PR_GetString(def->s_name));

	i = strlen(line);
	for ( ; i<20 ; i++)
		strlcat (line, " ", sizeof (line));
	strlcat (line, " ", sizeof (line));

	return line;
}


/*
=============
ED_Print

For debugging
=============
*/
// LordHavoc: optimized this to print out much more quickly (tempstring)
// LordHavoc: changed to print out every 4096 characters (incase there are a lot of fields to print)
void ED_Print(edict_t *ed)
{
	int		l;
	ddef_t	*d;
	int		*v;
	int		i, j;
	char	*name;
	int		type;
	char	tempstring[8192], tempstring2[260]; // temporary string buffers

	if (ed->e->free)
	{
		Con_Print("FREE\n");
		return;
	}

	tempstring[0] = 0;
	snprintf (tempstring, sizeof (tempstring), "\nEDICT %i:\n", NUM_FOR_EDICT(ed));
	for (i=1 ; i<progs->numfielddefs ; i++)
	{
		d = &pr_fielddefs[i];
		name = PR_GetString(d->s_name);
		if (name[strlen(name)-2] == '_')
			continue;	// skip _x, _y, _z vars

		v = (int *)((char *)ed->v + d->ofs*4);

	// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;

		for (j=0 ; j<type_size[type] ; j++)
			if (v[j])
				break;
		if (j == type_size[type])
			continue;

		if (strlen(name) > 256)
		{
			memcpy (tempstring2, name, 256);
			tempstring2[256] = tempstring2[257] = tempstring2[258] = '.';
			tempstring2[259] = 0;
			name = tempstring2;
		}
		strlcat (tempstring, name, sizeof (tempstring));
		for (l = strlen(name);l < 14;l++)
			strcat(tempstring, " ");
		strcat(tempstring, " ");

		name = PR_ValueString(d->type, (eval_t *)v);
		if (strlen(name) > 256)
		{
			memcpy(tempstring2, name, 256);
			tempstring2[256] = tempstring2[257] = tempstring2[258] = '.';
			tempstring2[259] = 0;
			name = tempstring2;
		}
		strlcat (tempstring, name, sizeof (tempstring));
		strlcat (tempstring, "\n", sizeof (tempstring));
		if (strlen(tempstring) >= 4096)
		{
			Con_Print(tempstring);
			tempstring[0] = 0;
		}
	}
	if (tempstring[0])
		Con_Print(tempstring);
}

/*
=============
ED_Write

For savegames
=============
*/
void ED_Write (qfile_t *f, edict_t *ed)
{
	ddef_t	*d;
	int		*v;
	int		i, j;
	char	*name;
	int		type;

	FS_Print(f, "{\n");

	if (ed->e->free)
	{
		FS_Print(f, "}\n");
		return;
	}

	for (i=1 ; i<progs->numfielddefs ; i++)
	{
		d = &pr_fielddefs[i];
		name = PR_GetString(d->s_name);
		if (name[strlen(name)-2] == '_')
			continue;	// skip _x, _y, _z vars

		v = (int *)((char *)ed->v + d->ofs*4);

	// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;
		for (j=0 ; j<type_size[type] ; j++)
			if (v[j])
				break;
		if (j == type_size[type])
			continue;

		FS_Printf(f,"\"%s\" ",name);
		FS_Printf(f,"\"%s\"\n", PR_UglyValueString(d->type, (eval_t *)v));
	}

	FS_Print(f, "}\n");
}

void ED_PrintNum (int ent)
{
	ED_Print(EDICT_NUM(ent));
}

/*
=============
ED_PrintEdicts

For debugging, prints all the entities in the current server
=============
*/
void ED_PrintEdicts (void)
{
	int		i;

	Con_Printf("%i entities\n", sv.num_edicts);
	for (i=0 ; i<sv.num_edicts ; i++)
		ED_PrintNum (i);
}

/*
=============
ED_PrintEdict_f

For debugging, prints a single edict
=============
*/
void ED_PrintEdict_f (void)
{
	int		i;

	i = atoi (Cmd_Argv(1));
	if (i < 0 || i >= sv.num_edicts)
	{
		Con_Print("Bad edict number\n");
		return;
	}
	ED_PrintNum (i);
}

/*
=============
ED_Count

For debugging
=============
*/
void ED_Count (void)
{
	int		i;
	edict_t	*ent;
	int		active, models, solid, step;

	active = models = solid = step = 0;
	for (i=0 ; i<sv.num_edicts ; i++)
	{
		ent = EDICT_NUM(i);
		if (ent->e->free)
			continue;
		active++;
		if (ent->v->solid)
			solid++;
		if (ent->v->model)
			models++;
		if (ent->v->movetype == MOVETYPE_STEP)
			step++;
	}

	Con_Printf("num_edicts:%3i\n", sv.num_edicts);
	Con_Printf("active    :%3i\n", active);
	Con_Printf("view      :%3i\n", models);
	Con_Printf("touch     :%3i\n", solid);
	Con_Printf("step      :%3i\n", step);

}

/*
==============================================================================

					ARCHIVING GLOBALS

FIXME: need to tag constants, doesn't really work
==============================================================================
*/

/*
=============
ED_WriteGlobals
=============
*/
void ED_WriteGlobals (qfile_t *f)
{
	ddef_t		*def;
	int			i;
	char		*name;
	int			type;

	FS_Print(f,"{\n");
	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		type = def->type;
		if ( !(def->type & DEF_SAVEGLOBAL) )
			continue;
		type &= ~DEF_SAVEGLOBAL;

		if (type != ev_string && type != ev_float && type != ev_entity)
			continue;

		name = PR_GetString(def->s_name);
		FS_Printf(f,"\"%s\" ", name);
		FS_Printf(f,"\"%s\"\n", PR_UglyValueString(type, (eval_t *)&pr_globals[def->ofs]));
	}
	FS_Print(f,"}\n");
}

/*
=============
ED_EdictSet_f

Console command to set a field of a specified edict
=============
*/
void ED_EdictSet_f(void)
{
	edict_t *ed;
	ddef_t *key;

	if(Cmd_Argc() != 4)
	{
		Con_Print("edictset <edict number> <field> <value>\n");
		return;
	}
	ed = EDICT_NUM(atoi(Cmd_Argv(1)));

	if((key = ED_FindField(Cmd_Argv(2))) == 0)
	{
		Con_Printf("Key %s not found !\n", Cmd_Argv(2));
		return;
	}

	ED_ParseEpair(ed, key, Cmd_Argv(3));
}

/*
=============
ED_ParseGlobals
=============
*/
void ED_ParseGlobals (const char *data)
{
	char keyname[1024]; // LordHavoc: good idea? bad idea?  was 64
	ddef_t *key;

	while (1)
	{
		// parse key
		if (!COM_ParseToken(&data, false))
			Host_Error ("ED_ParseEntity: EOF without closing brace");
		if (com_token[0] == '}')
			break;

		strcpy (keyname, com_token);

		// parse value
		if (!COM_ParseToken(&data, false))
			Host_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			Host_Error ("ED_ParseEntity: closing brace without data");

		key = ED_FindGlobal (keyname);
		if (!key)
		{
			Con_DPrintf("'%s' is not a global\n", keyname);
			continue;
		}

		if (!ED_ParseEpair(NULL, key, com_token))
			Host_Error ("ED_ParseGlobals: parse error");
	}
}

//============================================================================


/*
=============
ED_NewString
=============
*/
char *ED_NewString (const char *string)
{
	char *new, *new_p;
	int i,l;

	l = strlen(string) + 1;
	new = Mem_Alloc(edictstring_mempool, l);
	new_p = new;

	for (i=0 ; i< l ; i++)
	{
		if (string[i] == '\\' && i < l-1)
		{
			i++;
			if (string[i] == 'n')
				*new_p++ = '\n';
			else
				*new_p++ = '\\';
		}
		else
			*new_p++ = string[i];
	}

	return new;
}


/*
=============
ED_ParseEval

Can parse either fields or globals
returns false if error
=============
*/
qboolean ED_ParseEpair(edict_t *ent, ddef_t *key, const char *s)
{
	int i;
	ddef_t *def;
	eval_t *val;
	mfunction_t *func;

	if (ent)
		val = (eval_t *)((int *)ent->v + key->ofs);
	else
		val = (eval_t *)((int *)pr_globals + key->ofs);
	switch (key->type & ~DEF_SAVEGLOBAL)
	{
	case ev_string:
		val->string = PR_SetString(ED_NewString(s));
		break;

	case ev_float:
		while (*s && *s <= ' ')
			s++;
		val->_float = atof(s);
		break;

	case ev_vector:
		for (i = 0;i < 3;i++)
		{
			while (*s && *s <= ' ')
				s++;
			if (*s)
				val->vector[i] = atof(s);
			else
				val->vector[i] = 0;
			while (*s > ' ')
				s++;
		}
		break;

	case ev_entity:
		while (*s && *s <= ' ')
			s++;
		i = atoi(s);
		if (i < 0 || i >= MAX_EDICTS)
			Con_Printf("ED_ParseEpair: ev_entity reference too large (edict %i >= MAX_EDICTS %i)\n", i, MAX_EDICTS);
		while (i >= sv.max_edicts)
			SV_IncreaseEdicts();
		// if SV_IncreaseEdicts was called the base pointer needs to be updated
		if (ent)
			val = (eval_t *)((int *)ent->v + key->ofs);
		val->edict = EDICT_TO_PROG(EDICT_NUM(i));
		break;

	case ev_field:
		def = ED_FindField(s);
		if (!def)
		{
			Con_DPrintf("ED_ParseEpair: Can't find field %s\n", s);
			return false;
		}
		//val->_int = G_INT(def->ofs); AK Please check this - seems to be an org. quake bug
		val->_int = def->ofs;
		break;

	case ev_function:
		func = ED_FindFunction(s);
		if (!func)
		{
			Con_Printf("ED_ParseEpair: Can't find function %s\n", s);
			return false;
		}
		val->function = func - pr_functions;
		break;

	default:
		Con_Printf("ED_ParseEpair: Unknown key->type %i for key \"%s\"\n", key->type, PR_GetString(key->s_name));
		return false;
	}
	return true;
}

/*
====================
ED_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
Used for initial level load and for savegames.
====================
*/
const char *ED_ParseEdict (const char *data, edict_t *ent)
{
	ddef_t *key;
	qboolean anglehack;
	qboolean init;
	char keyname[256];
	int n;

	init = false;

// clear it
	if (ent != sv.edicts)	// hack
		memset (ent->v, 0, progs->entityfields * 4);

// go through all the dictionary pairs
	while (1)
	{
	// parse key
		if (!COM_ParseToken(&data, false))
			Host_Error ("ED_ParseEntity: EOF without closing brace");
		if (com_token[0] == '}')
			break;

		// anglehack is to allow QuakeEd to write single scalar angles
		// and allow them to be turned into vectors. (FIXME...)
		anglehack = !strcmp (com_token, "angle");
		if (anglehack)
			strlcpy (com_token, "angles", sizeof (com_token));

		// FIXME: change light to _light to get rid of this hack
		if (!strcmp(com_token, "light"))
			strlcpy (com_token, "light_lev", sizeof (com_token));	// hack for single light def

		strlcpy (keyname, com_token, sizeof (keyname));

		// another hack to fix heynames with trailing spaces
		n = strlen(keyname);
		while (n && keyname[n-1] == ' ')
		{
			keyname[n-1] = 0;
			n--;
		}

	// parse value
		if (!COM_ParseToken(&data, false))
			Host_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			Host_Error ("ED_ParseEntity: closing brace without data");

		init = true;

// keynames with a leading underscore are used for utility comments,
// and are immediately discarded by quake
		if (keyname[0] == '_')
			continue;

		key = ED_FindField (keyname);
		if (!key)
		{
			Con_DPrintf("'%s' is not a field\n", keyname);
			continue;
		}

		if (anglehack)
		{
			char	temp[32];
			strlcpy (temp, com_token, sizeof (temp));
			snprintf (com_token, sizeof (com_token), "0 %s 0", temp);
		}

		if (!ED_ParseEpair(ent, key, com_token))
			Host_Error ("ED_ParseEdict: parse error");
	}

	if (!init)
		ent->e->free = true;

	return data;
}


/*
================
ED_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.

Used for both fresh maps and savegame loads.  A fresh map would also need
to call ED_CallSpawnFunctions () to let the objects initialize themselves.
================
*/
void ED_LoadFromFile (const char *data)
{
	edict_t *ent;
	int parsed, inhibited, spawned, died;
	mfunction_t *func;

	ent = NULL;
	parsed = 0;
	inhibited = 0;
	spawned = 0;
	died = 0;
	pr_global_struct->time = sv.time;

// parse ents
	while (1)
	{
// parse the opening brace
		if (!COM_ParseToken(&data, false))
			break;
		if (com_token[0] != '{')
			Host_Error ("ED_LoadFromFile: found %s when expecting {",com_token);

		if (!ent)
			ent = EDICT_NUM(0);
		else
			ent = ED_Alloc ();
		data = ED_ParseEdict (data, ent);
		parsed++;

// remove things from different skill levels or deathmatch
		if (deathmatch.integer)
		{
			if (((int)ent->v->spawnflags & SPAWNFLAG_NOT_DEATHMATCH))
			{
				ED_Free (ent);
				inhibited++;
				continue;
			}
		}
		else if ((current_skill == 0 && ((int)ent->v->spawnflags & SPAWNFLAG_NOT_EASY  ))
			  || (current_skill == 1 && ((int)ent->v->spawnflags & SPAWNFLAG_NOT_MEDIUM))
			  || (current_skill >= 2 && ((int)ent->v->spawnflags & SPAWNFLAG_NOT_HARD  )))
		{
			ED_Free (ent);
			inhibited++;
			continue;
		}

//
// immediately call spawn function
//
		if (!ent->v->classname)
		{
			Con_Print("No classname for:\n");
			ED_Print(ent);
			ED_Free (ent);
			continue;
		}

	// look for the spawn function
		func = ED_FindFunction (PR_GetString(ent->v->classname));

		if (!func)
		{
			if (developer.integer) // don't confuse non-developers with errors
			{
				Con_Print("No spawn function for:\n");
				ED_Print(ent);
			}
			ED_Free (ent);
			continue;
		}

		pr_global_struct->self = EDICT_TO_PROG(ent);
		PR_ExecuteProgram (func - pr_functions, "QC function spawn is missing");
		spawned++;
		if (ent->e->free)
			died++;
	}

	Con_DPrintf("%i entities parsed, %i inhibited, %i spawned (%i removed self, %i stayed)\n", parsed, inhibited, spawned, died, spawned - died);
}


typedef struct dpfield_s
{
	int type;
	char *string;
}
dpfield_t;

#define DPFIELDS (sizeof(dpfields) / sizeof(dpfield_t))

dpfield_t dpfields[] =
{
	{ev_entity, "cursor_trace_ent"},
	{ev_entity, "drawonlytoclient"},
	{ev_entity, "exteriormodeltoclient"},
	{ev_entity, "nodrawtoclient"},
	{ev_entity, "tag_entity"},
	{ev_entity, "viewmodelforclient"},
	{ev_float, "alpha"},
	{ev_float, "ammo_cells1"},
	{ev_float, "ammo_lava_nails"},
	{ev_float, "ammo_multi_rockets"},
	{ev_float, "ammo_nails1"},
	{ev_float, "ammo_plasma"},
	{ev_float, "ammo_rockets1"},
	{ev_float, "ammo_shells1"},
	{ev_float, "button3"},
	{ev_float, "button4"},
	{ev_float, "button5"},
	{ev_float, "button6"},
	{ev_float, "button7"},
	{ev_float, "button8"},
	{ev_float, "buttonchat"},
	{ev_float, "buttonuse"},
	{ev_float, "clientcolors"},
	{ev_float, "cursor_active"},
	{ev_float, "fullbright"},
	{ev_float, "glow_color"},
	{ev_float, "glow_size"},
	{ev_float, "glow_trail"},
	{ev_float, "gravity"},
	{ev_float, "idealpitch"},
	{ev_float, "items2"},
	{ev_float, "light_lev"},
	{ev_float, "pflags"},
	{ev_float, "ping"},
	{ev_float, "pitch_speed"},
	{ev_float, "pmodel"},
	{ev_float, "renderamt"}, // HalfLife support
	{ev_float, "rendermode"}, // HalfLife support
	{ev_float, "scale"},
	{ev_float, "style"},
	{ev_float, "tag_index"},
	{ev_float, "viewzoom"},
	{ev_vector, "color"},
	{ev_vector, "colormod"},
	{ev_vector, "cursor_screen"},
	{ev_vector, "cursor_trace_endpos"},
	{ev_vector, "cursor_trace_start"},
	{ev_vector, "movement"},
	{ev_vector, "punchvector"},
	{ev_string, "playermodel"},
	{ev_string, "playerskin"}
};

/*
===============
PR_LoadProgs
===============
*/
extern void PR_Cmd_Reset (void);
void PR_LoadProgs (void)
{
	int i;
	dstatement_t *st;
	ddef_t *infielddefs;
	dfunction_t *dfunctions;

// flush the non-C variable lookup cache
	for (i=0 ; i<GEFV_CACHESIZE ; i++)
		gefvCache[i].field[0] = 0;

	Mem_EmptyPool(progs_mempool);
	Mem_EmptyPool(edictstring_mempool);

	progs = (dprograms_t *)FS_LoadFile ("progs.dat", progs_mempool, false);
	if (!progs)
		Host_Error ("PR_LoadProgs: couldn't load progs.dat");

	Con_DPrintf("Programs occupy %iK.\n", fs_filesize/1024);

	pr_crc = CRC_Block((qbyte *)progs, fs_filesize);

// byte swap the header
	for (i = 0;i < (int) sizeof(*progs) / 4;i++)
		((int *)progs)[i] = LittleLong ( ((int *)progs)[i] );

	if (progs->version != PROG_VERSION)
		Host_Error ("progs.dat has wrong version number (%i should be %i)", progs->version, PROG_VERSION);
	if (progs->crc != PROGHEADER_CRC && progs->crc != 32401) // tenebrae crc also allowed
		Host_Error ("progs.dat system vars have been modified, progdefs.h is out of date");

	//pr_functions = (dfunction_t *)((qbyte *)progs + progs->ofs_functions);
	dfunctions = (dfunction_t *)((qbyte *)progs + progs->ofs_functions);
	pr_strings = (char *)progs + progs->ofs_strings;
	pr_globaldefs = (ddef_t *)((qbyte *)progs + progs->ofs_globaldefs);

	// we need to expand the fielddefs list to include all the engine fields,
	// so allocate a new place for it
	infielddefs = (ddef_t *)((qbyte *)progs + progs->ofs_fielddefs);
	pr_fielddefs = Mem_Alloc(progs_mempool, (progs->numfielddefs + DPFIELDS) * sizeof(ddef_t));

	pr_statements = (dstatement_t *)((qbyte *)progs + progs->ofs_statements);

	// moved edict_size calculation down below field adding code

	pr_global_struct = (globalvars_t *)((qbyte *)progs + progs->ofs_globals);
	pr_globals = (float *)pr_global_struct;

// byte swap the lumps
	for (i=0 ; i<progs->numstatements ; i++)
	{
		pr_statements[i].op = LittleShort(pr_statements[i].op);
		pr_statements[i].a = LittleShort(pr_statements[i].a);
		pr_statements[i].b = LittleShort(pr_statements[i].b);
		pr_statements[i].c = LittleShort(pr_statements[i].c);
	}

	pr_functions = Mem_Alloc(progs_mempool, sizeof(mfunction_t) * progs->numfunctions);
	for (i = 0;i < progs->numfunctions;i++)
	{
		pr_functions[i].first_statement = LittleLong (dfunctions[i].first_statement);
		pr_functions[i].parm_start = LittleLong (dfunctions[i].parm_start);
		pr_functions[i].s_name = LittleLong (dfunctions[i].s_name);
		pr_functions[i].s_file = LittleLong (dfunctions[i].s_file);
		pr_functions[i].numparms = LittleLong (dfunctions[i].numparms);
		pr_functions[i].locals = LittleLong (dfunctions[i].locals);
		memcpy(pr_functions[i].parm_size, dfunctions[i].parm_size, sizeof(dfunctions[i].parm_size));
	}

	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		pr_globaldefs[i].type = LittleShort (pr_globaldefs[i].type);
		pr_globaldefs[i].ofs = LittleShort (pr_globaldefs[i].ofs);
		pr_globaldefs[i].s_name = LittleLong (pr_globaldefs[i].s_name);
	}

	// copy the progs fields to the new fields list
	for (i = 0;i < progs->numfielddefs;i++)
	{
		pr_fielddefs[i].type = LittleShort (infielddefs[i].type);
		if (pr_fielddefs[i].type & DEF_SAVEGLOBAL)
			Host_Error ("PR_LoadProgs: pr_fielddefs[i].type & DEF_SAVEGLOBAL");
		pr_fielddefs[i].ofs = LittleShort (infielddefs[i].ofs);
		pr_fielddefs[i].s_name = LittleLong (infielddefs[i].s_name);
	}

	// append the darkplaces fields
	for (i = 0;i < (int) DPFIELDS;i++)
	{
		pr_fielddefs[progs->numfielddefs].type = dpfields[i].type;
		pr_fielddefs[progs->numfielddefs].ofs = progs->entityfields;
		pr_fielddefs[progs->numfielddefs].s_name = PR_SetString(dpfields[i].string);
		if (pr_fielddefs[progs->numfielddefs].type == ev_vector)
			progs->entityfields += 3;
		else
			progs->entityfields++;
		progs->numfielddefs++;
	}

	for (i=0 ; i<progs->numglobals ; i++)
		((int *)pr_globals)[i] = LittleLong (((int *)pr_globals)[i]);

	// moved edict_size calculation down here, below field adding code
	// LordHavoc: this no longer includes the edict_t header
	pr_edict_size = progs->entityfields * 4;
	pr_edictareasize = pr_edict_size * MAX_EDICTS;

	// LordHavoc: bounds check anything static
	for (i = 0,st = pr_statements;i < progs->numstatements;i++,st++)
	{
		switch (st->op)
		{
		case OP_IF:
		case OP_IFNOT:
			if ((unsigned short) st->a >= progs->numglobals || st->b + i < 0 || st->b + i >= progs->numstatements)
				Host_Error("PR_LoadProgs: out of bounds IF/IFNOT (statement %d)\n", i);
			break;
		case OP_GOTO:
			if (st->a + i < 0 || st->a + i >= progs->numstatements)
				Host_Error("PR_LoadProgs: out of bounds GOTO (statement %d)\n", i);
			break;
		// global global global
		case OP_ADD_F:
		case OP_ADD_V:
		case OP_SUB_F:
		case OP_SUB_V:
		case OP_MUL_F:
		case OP_MUL_V:
		case OP_MUL_FV:
		case OP_MUL_VF:
		case OP_DIV_F:
		case OP_BITAND:
		case OP_BITOR:
		case OP_GE:
		case OP_LE:
		case OP_GT:
		case OP_LT:
		case OP_AND:
		case OP_OR:
		case OP_EQ_F:
		case OP_EQ_V:
		case OP_EQ_S:
		case OP_EQ_E:
		case OP_EQ_FNC:
		case OP_NE_F:
		case OP_NE_V:
		case OP_NE_S:
		case OP_NE_E:
		case OP_NE_FNC:
		case OP_ADDRESS:
		case OP_LOAD_F:
		case OP_LOAD_FLD:
		case OP_LOAD_ENT:
		case OP_LOAD_S:
		case OP_LOAD_FNC:
		case OP_LOAD_V:
			if ((unsigned short) st->a >= progs->numglobals || (unsigned short) st->b >= progs->numglobals || (unsigned short) st->c >= progs->numglobals)
				Host_Error("PR_LoadProgs: out of bounds global index (statement %d)\n", i);
			break;
		// global none global
		case OP_NOT_F:
		case OP_NOT_V:
		case OP_NOT_S:
		case OP_NOT_FNC:
		case OP_NOT_ENT:
			if ((unsigned short) st->a >= progs->numglobals || (unsigned short) st->c >= progs->numglobals)
				Host_Error("PR_LoadProgs: out of bounds global index (statement %d)\n", i);
			break;
		// 2 globals
		case OP_STOREP_F:
		case OP_STOREP_ENT:
		case OP_STOREP_FLD:
		case OP_STOREP_S:
		case OP_STOREP_FNC:
		case OP_STORE_F:
		case OP_STORE_ENT:
		case OP_STORE_FLD:
		case OP_STORE_S:
		case OP_STORE_FNC:
		case OP_STATE:
		case OP_STOREP_V:
		case OP_STORE_V:
			if ((unsigned short) st->a >= progs->numglobals || (unsigned short) st->b >= progs->numglobals)
				Host_Error("PR_LoadProgs: out of bounds global index (statement %d)\n", i);
			break;
		// 1 global
		case OP_CALL0:
		case OP_CALL1:
		case OP_CALL2:
		case OP_CALL3:
		case OP_CALL4:
		case OP_CALL5:
		case OP_CALL6:
		case OP_CALL7:
		case OP_CALL8:
		case OP_DONE:
		case OP_RETURN:
			if ((unsigned short) st->a >= progs->numglobals)
				Host_Error("PR_LoadProgs: out of bounds global index (statement %d)\n", i);
			break;
		default:
			Host_Error("PR_LoadProgs: unknown opcode %d at statement %d\n", st->op, i);
			break;
		}
	}

	FindEdictFieldOffsets(); // LordHavoc: update field offset list
	PR_Execute_ProgsLoaded();
	PR_Cmd_Reset();
}


void PR_Fields_f (void)
{
	int i, j, ednum, used, usedamount;
	int *counts;
	char tempstring[5000], tempstring2[260], *name;
	edict_t *ed;
	ddef_t *d;
	int *v;
	if (!sv.active)
	{
		Con_Print("no progs loaded\n");
		return;
	}
	counts = Mem_Alloc(tempmempool, progs->numfielddefs * sizeof(int));
	for (ednum = 0;ednum < sv.max_edicts;ednum++)
	{
		ed = EDICT_NUM(ednum);
		if (ed->e->free)
			continue;
		for (i = 1;i < progs->numfielddefs;i++)
		{
			d = &pr_fielddefs[i];
			name = PR_GetString(d->s_name);
			if (name[strlen(name)-2] == '_')
				continue;	// skip _x, _y, _z vars
			v = (int *)((char *)ed->v + d->ofs*4);
			// if the value is still all 0, skip the field
			for (j = 0;j < type_size[d->type & ~DEF_SAVEGLOBAL];j++)
			{
				if (v[j])
				{
					counts[i]++;
					break;
				}
			}
		}
	}
	used = 0;
	usedamount = 0;
	tempstring[0] = 0;
	for (i = 0;i < progs->numfielddefs;i++)
	{
		d = &pr_fielddefs[i];
		name = PR_GetString(d->s_name);
		if (name[strlen(name)-2] == '_')
			continue;	// skip _x, _y, _z vars
		switch(d->type & ~DEF_SAVEGLOBAL)
		{
		case ev_string:
			strlcat (tempstring, "string   ", sizeof (tempstring));
			break;
		case ev_entity:
			strlcat (tempstring, "entity   ", sizeof (tempstring));
			break;
		case ev_function:
			strlcat (tempstring, "function ", sizeof (tempstring));
			break;
		case ev_field:
			strlcat (tempstring, "field    ", sizeof (tempstring));
			break;
		case ev_void:
			strlcat (tempstring, "void     ", sizeof (tempstring));
			break;
		case ev_float:
			strlcat (tempstring, "float    ", sizeof (tempstring));
			break;
		case ev_vector:
			strlcat (tempstring, "vector   ", sizeof (tempstring));
			break;
		case ev_pointer:
			strlcat (tempstring, "pointer  ", sizeof (tempstring));
			break;
		default:
			snprintf (tempstring2, sizeof (tempstring2), "bad type %i ", d->type & ~DEF_SAVEGLOBAL);
			strlcat (tempstring, tempstring2, sizeof (tempstring));
			break;
		}
		if (strlen(name) > 256)
		{
			memcpy(tempstring2, name, 256);
			tempstring2[256] = tempstring2[257] = tempstring2[258] = '.';
			tempstring2[259] = 0;
			name = tempstring2;
		}
		strcat (tempstring, name);
		for (j = strlen(name);j < 25;j++)
			strcat(tempstring, " ");
		snprintf (tempstring2, sizeof (tempstring2), "%5d", counts[i]);
		strlcat (tempstring, tempstring2, sizeof (tempstring));
		strlcat (tempstring, "\n", sizeof (tempstring));
		if (strlen(tempstring) >= 4096)
		{
			Con_Print(tempstring);
			tempstring[0] = 0;
		}
		if (counts[i])
		{
			used++;
			usedamount += type_size[d->type & ~DEF_SAVEGLOBAL];
		}
	}
	Mem_Free(counts);
	Con_Printf("%i entity fields (%i in use), totalling %i bytes per edict (%i in use), %i edicts allocated, %i bytes total spent on edict fields (%i needed)\n", progs->entityfields, used, progs->entityfields * 4, usedamount * 4, sv.max_edicts, progs->entityfields * 4 * sv.max_edicts, usedamount * 4 * sv.max_edicts);
}

void PR_Globals_f (void)
{
	int i;
	if (!sv.active)
	{
		Con_Print("no progs loaded\n");
		return;
	}
	for (i = 0;i < progs->numglobaldefs;i++)
		Con_Printf("%s\n", PR_GetString(pr_globaldefs[i].s_name));
	Con_Printf("%i global variables, totalling %i bytes\n", progs->numglobals, progs->numglobals * 4);
}

/*
===============
PR_Init
===============
*/
extern void PR_Cmd_Init(void);
void PR_Init (void)
{
	Cmd_AddCommand ("edict", ED_PrintEdict_f);
	Cmd_AddCommand ("edicts", ED_PrintEdicts);
	Cmd_AddCommand ("edictcount", ED_Count);
	Cmd_AddCommand ("edictset", ED_EdictSet_f);
	Cmd_AddCommand ("profile", PR_Profile_f);
	Cmd_AddCommand ("pr_fields", PR_Fields_f);
	Cmd_AddCommand ("pr_globals", PR_Globals_f);
	Cvar_RegisterVariable (&pr_checkextension);
	Cvar_RegisterVariable (&nomonsters);
	Cvar_RegisterVariable (&gamecfg);
	Cvar_RegisterVariable (&scratch1);
	Cvar_RegisterVariable (&scratch2);
	Cvar_RegisterVariable (&scratch3);
	Cvar_RegisterVariable (&scratch4);
	Cvar_RegisterVariable (&savedgamecfg);
	Cvar_RegisterVariable (&saved1);
	Cvar_RegisterVariable (&saved2);
	Cvar_RegisterVariable (&saved3);
	Cvar_RegisterVariable (&saved4);
	// LordHavoc: for DarkPlaces, this overrides the number of decors (shell casings, gibs, etc)
	Cvar_RegisterVariable (&decors);
	// LordHavoc: Nehahra uses these to pass data around cutscene demos
	if (gamemode == GAME_NEHAHRA)
	{
		Cvar_RegisterVariable (&nehx00);Cvar_RegisterVariable (&nehx01);
		Cvar_RegisterVariable (&nehx02);Cvar_RegisterVariable (&nehx03);
		Cvar_RegisterVariable (&nehx04);Cvar_RegisterVariable (&nehx05);
		Cvar_RegisterVariable (&nehx06);Cvar_RegisterVariable (&nehx07);
		Cvar_RegisterVariable (&nehx08);Cvar_RegisterVariable (&nehx09);
		Cvar_RegisterVariable (&nehx10);Cvar_RegisterVariable (&nehx11);
		Cvar_RegisterVariable (&nehx12);Cvar_RegisterVariable (&nehx13);
		Cvar_RegisterVariable (&nehx14);Cvar_RegisterVariable (&nehx15);
		Cvar_RegisterVariable (&nehx16);Cvar_RegisterVariable (&nehx17);
		Cvar_RegisterVariable (&nehx18);Cvar_RegisterVariable (&nehx19);
	}
	Cvar_RegisterVariable (&cutscene); // for Nehahra but useful to other mods as well
	// LordHavoc: optional runtime bounds checking (speed drain, but worth it for security, on by default - breaks most QCCX features (used by CRMod and others))
	Cvar_RegisterVariable (&pr_boundscheck);
	Cvar_RegisterVariable (&pr_traceqc);

	progs_mempool = Mem_AllocPool("progs.dat", 0, NULL);
	edictstring_mempool = Mem_AllocPool("edict strings", 0, NULL);

	PR_Cmd_Init();
}

// LordHavoc: turned EDICT_NUM into a #define for speed reasons
edict_t *EDICT_NUM_ERROR(int n, char *filename, int fileline)
{
	Host_Error ("EDICT_NUM: bad number %i (called at %s:%i)", n, filename, fileline);
	return NULL;
}

/*
int NUM_FOR_EDICT_ERROR(edict_t *e)
{
	Host_Error ("NUM_FOR_EDICT: bad pointer %p (world is %p, entity number would be %i)", e, sv.edicts, e - sv.edicts);
	return 0;
}

int NUM_FOR_EDICT(edict_t *e)
{
	int n;
	n = e - sv.edicts;
	if ((unsigned int)n >= MAX_EDICTS)
		Host_Error ("NUM_FOR_EDICT: bad pointer");
	return n;
}

//int NoCrash_NUM_FOR_EDICT(edict_t *e)
//{
//	return e - sv.edicts;
//}

//#define	EDICT_TO_PROG(e) ((qbyte *)(((edict_t *)e)->v) - (qbyte *)(sv.edictsfields))
//#define PROG_TO_EDICT(e) (sv.edicts + ((e) / (progs->entityfields * 4)))
int EDICT_TO_PROG(edict_t *e)
{
	int n;
	n = e - sv.edicts;
	if ((unsigned int)n >= (unsigned int)sv.max_edicts)
		Host_Error("EDICT_TO_PROG: invalid edict %8p (number %i compared to world at %8p)\n", e, n, sv.edicts);
	return n;// EXPERIMENTAL
	//return (qbyte *)e->v - (qbyte *)sv.edictsfields;
}
edict_t *PROG_TO_EDICT(int n)
{
	if ((unsigned int)n >= (unsigned int)sv.max_edicts)
		Host_Error("PROG_TO_EDICT: invalid edict number %i\n", n);
	return sv.edicts + n; // EXPERIMENTAL
	//return sv.edicts + ((n) / (progs->entityfields * 4));
}
*/

