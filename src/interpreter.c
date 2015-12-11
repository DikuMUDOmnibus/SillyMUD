/*
  SillyMUD Distribution V1.1b             (c) 1993 SillyMUD Developement
 
  See license.doc for distribution terms.   SillyMUD is based on DIKUMUD
*/
#include "config.h"

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <arpa/telnet.h>
#include <unistd.h>

#include "protos.h"
#include "parser.h"
#include "act.move.h"
#include "act.info.h"
#include "act.obj1.h"
#include "skills.h"
#include "act.obj2.h"
#include "act.info.h"
#include "act.comm.h"
#include "act.other.h"
#include "act.off.h"
#include "intrinsics.h"
#include "act.wizard.h"
#include "modify.h"
#include "create.h"
#include "act.social.h"
#include "spell_parser.h"

#define NOT !
#define AND &&
#define OR ||

#define STATE(d) ((d)->connected)
#define MAX_CMD_LIST 400

extern struct RaceChoices RaceList[RACE_LIST_SIZE];
extern const struct title_type titles[MAX_CLASS][ABS_MAX_LVL];
extern char motd[MAX_STRING_LENGTH];
extern char wmotd[MAX_STRING_LENGTH];
extern struct char_data *character_list;
extern struct player_index_element *player_table;
extern int top_of_p_table;
extern struct index_data *mob_index;
extern struct index_data *obj_index;
#if HASH
extern struct hash_header room_db;
#else
extern struct room_data *room_db;
#endif



unsigned char echo_on[] = { IAC, WONT, TELOPT_ECHO, '\r', '\n', '\0' };
unsigned char echo_off[] = { IAC, WILL, TELOPT_ECHO, '\0' };

int WizLock;
int Silence = 0;
int plr_tick_count = 0;

#if PLAYER_AUTH
void do_auth(struct char_data *ch, char *arg, int cmd); /* jdb 3-1 */
#endif




char *fill[] = { "in",
  "from",
  "with",
  "the",
  "on",
  "at",
  "to",
  "\n"
};




int search_block(char *arg, char **list, bool exact) {
  register int i, l;

  /* Make into lower case, and get length of string */
  for (l = 0; *(arg + l); l++)
    *(arg + l) = LOWER(*(arg + l));

  if (exact) {
    for (i = 0; **(list + i) != '\n'; i++)
      if (!strcmp(arg, *(list + i)))
        return (i);
  }
  else {
    if (!l)
      l = 1;                    /* Avoid "" to match the first available string */
    for (i = 0; **(list + i) != '\n'; i++)
      if (!strncmp(arg, *(list + i), l))
        return (i);
  }

  return (-1);
}


int old_search_block(char *argument, int begin, int length, char **list,
                     int mode) {
  int guess, found, search;


  /* If the word contain 0 letters, then a match is already found */
  found = (length < 1);

  guess = 0;

  /* Search for a match */

  if (mode)
    while (NOT found AND * (list[guess]) != '\n') {
      found = ((size_t) length == strlen(list[guess]));
      for (search = 0; (search < length AND found); search++)
        found = (*(argument + begin + search) == *(list[guess] + search));
      guess++;
    }
  else {
    while (NOT found AND * (list[guess]) != '\n') {
      found = 1;
      for (search = 0; (search < length AND found); search++)
        found = (*(argument + begin + search) == *(list[guess] + search));
      guess++;
    }
  }

  return (found ? guess : -1);
}

void command_interpreter(struct char_data *ch, char *argument) {

  char buf[200];
  extern int no_specials;
  NODE *n;
  char buf1[255], buf2[255];

  REMOVE_BIT(ch->specials.affected_by, AFF_HIDE);

  if (MOUNTED(ch)) {
    if (ch->in_room != MOUNTED(ch)->in_room)
      dismount(ch, MOUNTED(ch), POSITION_STANDING);
  }

  /*
   *  a bug check.
   */
  if (!IS_NPC(ch)) {
    int found = FALSE;
    size_t i;
    if ((!ch->player.name[0]) || (ch->player.name[0] < ' ')) {
      log_msg("Error in character name.  Changed to 'Error'");
      free(ch->player.name);
      ch->player.name = (char *)malloc(6);
      strcpy(ch->player.name, "Error");
      return;
    }
    strcpy(buf, ch->player.name);
    for (i = 0; i < strlen(buf) && !found; i++) {
      if (buf[i] < 65) {
        found = TRUE;
      }
    }
    if (found) {
      log_msg("Error in character name.  Changed to 'Error'");
      free(ch->player.name);
      ch->player.name = (char *)malloc(6);
      strcpy(ch->player.name, "Error");
      return;
    }
  }
  if (!*argument || *argument == '\n') {
    return;
  }
  else if (!isalpha(*argument)) {
    buf1[0] = *argument;
    buf1[1] = '\0';
    if ((argument + 1))
      strcpy(buf2, (argument + 1));
    else
      buf2[0] = '\0';
  }
  else {
    register int i;
    half_chop(argument, buf1, buf2);
    i = 0;
    while (buf1[i] != '\0') {
      buf1[i] = LOWER(buf1[i]);
      i++;
    }
  }

/* New parser by DM */
  if (*buf1)
    n = find_valid_command(buf1);
  else
    n = NULL;
/*  
  cmd = old_search_block(argument,begin,look_at,command,0);
*/
  if (!n) {
    send_to_char("Pardon?\n\r", ch);
    return;
  }

  if (get_max_level(ch) < n->min_level) {
    send_to_char("Pardon?\n\r", ch);
    return;
  }

  if ((n->func != 0) || (n->func_dep)) {
    if ((!IS_AFFECTED(ch, AFF_PARALYSIS)) || (n->min_pos <= POSITION_STUNNED)) {
      if (GET_POS(ch) < n->min_pos) {
        switch (GET_POS(ch)) {
        case POSITION_DEAD:
          send_to_char("Lie still; you are DEAD!!! :-( \n\r", ch);
          break;
        case POSITION_INCAP:
        case POSITION_MORTALLYW:
          send_to_char
            ("You are in a pretty bad shape, unable to do anything!\n\r", ch);
          break;

        case POSITION_STUNNED:
          send_to_char
            ("All you can do right now, is think about the stars!\n\r", ch);
          break;
        case POSITION_SLEEPING:
          send_to_char("In your dreams, or what?\n\r", ch);
          break;
        case POSITION_RESTING:
          send_to_char("Nah... You feel too relaxed to do that..\n\r", ch);
          break;
        case POSITION_SITTING:
          send_to_char("Maybe you should get on your feet first?\n\r", ch);
          break;
        case POSITION_FIGHTING:
          send_to_char("No way! You are fighting for your life!\n\r", ch);
          break;
        case POSITION_STANDING:
          send_to_char("Fraid you can't do that\n\r", ch);
          break;
        }
      }
      else {

        if (!no_specials && special(ch, n->number, buf2))
          return;

        if (n->log) {
          SPRINTF(buf, "%s:%s", ch->player.name, argument);
          slog(buf);
        }
        if ((get_max_level(ch) >= LOW_IMMORTAL) && (get_max_level(ch) < 60)) {
          SPRINTF(buf, "%s:%s", ch->player.name, argument);
          slog(buf);
        }
        if (IS_AFFECTED2(ch, AFF2_LOG_ME)) {
          SPRINTF(buf, "%s:%s", ch->player.name, argument);
          slog(buf);
        }
        if (GET_GOLD(ch) > 2000000) {
          SPRINTF(buf, "%s:%s", fname(ch->player.name), argument);
          slog(buf);
        }

        if (n->func)
          ((*n->func)
           (ch, buf2, n->name));
        else
          ((*n->func_dep)
           (ch, buf2, n->number));
      }
      return;
    }
    else {
      send_to_char(" You are paralyzed, you can't do much!\n\r", ch);
      return;
    }
  }
  if (n && !n->func && !n->func_dep)
    send_to_char("Sorry, but that command has yet to be implemented...\n\r",
                 ch);
  else
    send_to_char("Pardon? \n\r", ch);
}

void argument_interpreter(char *argument, char *first_arg, char *second_arg) {
  int look_at, begin;

  begin = 0;

  do {
    /* Find first non blank */
    for (; *(argument + begin) == ' '; begin++);

    /* Find length of first word */
    for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)

      /* Make all letters lower case,
         AND copy them to first_arg */
      *(first_arg + look_at) = LOWER(*(argument + begin + look_at));

    *(first_arg + look_at) = '\0';
    begin += look_at;

  }
  while (fill_word(first_arg));

  do {
    /* Find first non blank */
    for (; *(argument + begin) == ' '; begin++);

    /* Find length of first word */
    for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)

      /* Make all letters lower case,
         AND copy them to second_arg */
      *(second_arg + look_at) = LOWER(*(argument + begin + look_at));

    *(second_arg + look_at) = '\0';
    begin += look_at;

  }
  while (fill_word(second_arg));
}

int is_number(char *str) {
/*   int look_at; */

  if (*str == '\0')
    return (0);
  else if (newstrlen(str) > 8)
    return (0);
  else if ((atoi(str) == 0) && (str[0] != '0'))
    return (0);
  else
    return (1);

/*  for(look_at=0;*(str+look_at) != '\0';look_at++)
    if((*(str+look_at)<'0')||(*(str+look_at)>'9'))
      return(0);
  return(1); */
}

/*  Quinn substituted a new one-arg for the old one.. I thought returning a 
    char pointer would be neat, and avoiding the func-calls would save a
    little time... If anyone feels pissed, I'm sorry.. Anyhow, the code is
    snatched from the old one, so it outta work..
    
    void one_argument(char *argument,char *first_arg )
    {
    static char dummy[MAX_STRING_LENGTH];
    
    argument_interpreter(argument,first_arg,dummy);
    }
    
    */


/* find the first sub-argument of a string, return pointer to first char in
   primary argument, following the sub-arg			            */
char *one_argument(char *argument, char *first_arg) {
  int begin, look_at;

  begin = 0;

  do {
    /* Find first non blank */
    for (; isspace(*(argument + begin)); begin++);

    /* Find length of first word */
    for (look_at = 0; *(argument + begin + look_at) > ' '; look_at++)

      /* Make all letters lower case,
         AND copy them to first_arg */
      *(first_arg + look_at) = LOWER(*(argument + begin + look_at));

    *(first_arg + look_at) = '\0';
    begin += look_at;
  }
  while (fill_word(first_arg));

  return (argument + begin);
}



void only_argument(char *argument, char *dest) {
  while (*argument && isspace(*argument))
    argument++;
  strcpy(dest, argument);
}




int fill_word(char *argument) {
  return (search_block(argument, fill, TRUE) >= 0);
}





/* determine if a given string is an abbreviation of another */
int is_abbrev(char *arg1, char *arg2) {
  if (!*arg1)
    return (0);

  for (; *arg1; arg1++, arg2++)
    if (LOWER(*arg1) != LOWER(*arg2))
      return (0);

  return (1);
}




/* return first 'word' plus trailing substring of input string */
void half_chop(char *string, char *arg1, char *arg2) {
  for (; isspace(*string); string++);

  for (; !isspace(*arg1 = *string) && *string; string++, arg1++);

  *arg1 = '\0';

  for (; isspace(*string); string++);

  for (; (*arg2 = *string) != '\0'; string++, arg2++);
}



int special(struct char_data *ch, int cmd, char *arg) {
  register struct obj_data *i;
  register struct char_data *k;
  int j;


  if (ch->in_room == NOWHERE) {
    char_to_room(ch, 3001);
    return (0);
  }

  /* special in room? */
  if (real_roomp(ch->in_room)->funct)
    if ((*real_roomp(ch->in_room)->funct) (ch, cmd, arg,
                                           real_roomp(ch->in_room),
                                           PULSE_COMMAND))
      return (1);

  /* special in equipment list? */
  for (j = 0; j <= (MAX_WEAR - 1); j++)
    if (ch->equipment[j] && ch->equipment[j]->item_number >= 0)
      if (obj_index[ch->equipment[j]->item_number].func)
        if ((*obj_index[ch->equipment[j]->item_number].func)
            (ch, cmd, arg, ch->equipment[j], PULSE_COMMAND))
          return (1);

  /* special in inventory? */
  for (i = ch->carrying; i; i = i->next_content)
    if (i->item_number >= 0)
      if (obj_index[i->item_number].func)
        if ((*obj_index[i->item_number].func) (ch, cmd, arg, i, PULSE_COMMAND))
          return (1);

  /* special in mobile present? */
  for (k = real_roomp(ch->in_room)->people; k; k = k->next_in_room)
    if (IS_MOB(k))
      if (mob_index[k->nr].func)
        if ((*mob_index[k->nr].func) (ch, cmd, arg, k, PULSE_COMMAND))
          return (1);

  /* special in object present? */
  for (i = real_roomp(ch->in_room)->contents; i; i = i->next_content)
    if (i->item_number >= 0)
      if (obj_index[i->item_number].func)
        if ((*obj_index[i->item_number].func) (ch, cmd, arg, i, PULSE_COMMAND))
          return (1);

  return (0);
}

void assign_command_pointers() {
  init_radix();
  add_command("north", do_move, POSITION_STANDING, 0);
  add_command("east", do_move, POSITION_STANDING, 0);
  add_command("south", do_move, POSITION_STANDING, 0);
  add_command("west", do_move, POSITION_STANDING, 0);
  add_command("up", do_move, POSITION_STANDING, 0);
  add_command("down", do_move, POSITION_STANDING, 0);
  add_command("enter", do_enter, POSITION_STANDING, 0);
  add_command("exits", do_exits, POSITION_RESTING, 0);
  add_command("kiss", do_action, POSITION_RESTING, 0);
  add_command("get", do_get, POSITION_RESTING, 1);
  add_command("drink", do_drink, POSITION_RESTING, 1);
  add_command("eat", do_eat, POSITION_RESTING, 1);
  add_command("wear", do_wear, POSITION_RESTING, 0);
  add_command("wield", do_wield, POSITION_RESTING, 1);
  add_command("look", do_look, POSITION_RESTING, 0);
  add_command("score", do_score, POSITION_DEAD, 0);
  add_command("say", do_say, POSITION_RESTING, 0);
  add_command("shout", do_shout, POSITION_RESTING, 2);
  add_command("tell", do_tell, POSITION_RESTING, 0);
  add_command("inventory", do_inventory, POSITION_DEAD, 0);
  add_command("qui", do_qui, POSITION_DEAD, 0);
  add_command("bounce", do_action, POSITION_STANDING, 0);
  add_command("smile", do_action, POSITION_RESTING, 0);
  add_command("dance", do_action, POSITION_STANDING, 0);
  add_command("kill", do_kill, POSITION_FIGHTING, 1);
  add_command("cackle", do_action, POSITION_RESTING, 0);
  add_command("laugh", do_action, POSITION_RESTING, 0);
  add_command("giggle", do_action, POSITION_RESTING, 0);
  add_command("shake", do_action, POSITION_RESTING, 0);
  add_command("puke", do_action, POSITION_RESTING, 0);
  add_command("growl", do_action, POSITION_RESTING, 0);
  add_command("scream", do_action, POSITION_RESTING, 0);
  add_command("insult", do_insult, POSITION_RESTING, 0);
  add_command("comfort", do_action, POSITION_RESTING, 0);
  add_command("nod", do_action, POSITION_RESTING, 0);
  add_command("sigh", do_action, POSITION_RESTING, 0);
  add_command("sulk", do_action, POSITION_RESTING, 0);
  add_command("help", do_help, POSITION_DEAD, 0);
  add_command("who", do_who, POSITION_DEAD, 0);
  add_command("emote", do_emote, POSITION_SLEEPING, 0);
  add_command("echo", do_echo, POSITION_SLEEPING, 1);
  add_command("stand", do_stand, POSITION_RESTING, 0);
  add_command("sit", do_sit, POSITION_RESTING, 0);
  add_command("rest", do_rest, POSITION_RESTING, 0);
  add_command("sleep", do_sleep, POSITION_SLEEPING, 0);
  add_command("wake", do_wake, POSITION_SLEEPING, 0);
  add_command("force", do_force, POSITION_SLEEPING, LESSER_GOD);
  add_command("transfer", do_trans, POSITION_SLEEPING, DEMIGOD);
  add_command("hug", do_action, POSITION_RESTING, 0);
  add_command("snuggle", do_action, POSITION_RESTING, 0);
  add_command("cuddle", do_action, POSITION_RESTING, 0);
  add_command("nuzzle", do_action, POSITION_RESTING, 0);
  add_command("cry", do_action, POSITION_RESTING, 0);
  add_command("news", do_news, POSITION_SLEEPING, 0);
  add_command("equipment", do_equipment, POSITION_SLEEPING, 0);
  add_command("buy", do_not_here, POSITION_STANDING, 0);
  add_command("sell", do_not_here, POSITION_STANDING, 0);
  add_command("value", do_value, POSITION_RESTING, 0);
  add_command("list", do_not_here, POSITION_STANDING, 0);
  add_command("drop", do_drop, POSITION_RESTING, 1);
  add_command("goto", do_goto, POSITION_SLEEPING, 0);
  add_command("weather", do_weather, POSITION_RESTING, 0);
  add_command("read", do_read, POSITION_RESTING, 0);
  add_command("pour", do_pour, POSITION_STANDING, 0);
  add_command("grab", do_grab, POSITION_RESTING, 0);
  add_command("remove", do_remove, POSITION_RESTING, 0);
  add_command("put", do_put, POSITION_RESTING, 0);
  add_command("shutdow", do_shutdow, POSITION_DEAD, SILLYLORD);
  add_command("save", do_save, POSITION_SLEEPING, 0);
  add_command("hit", do_hit, POSITION_FIGHTING, 1);
  add_command("string", do_string, POSITION_SLEEPING, SAINT);
  add_command("give", do_give, POSITION_RESTING, 1);
  add_command("quit", do_quit, POSITION_DEAD, 0);
  add_command("stat", do_stat, POSITION_DEAD, CREATOR);
  add_command("guard", do_guard, POSITION_STANDING, 1);
  add_command("time", do_time, POSITION_DEAD, 0);
  add_command("load", do_load, POSITION_DEAD, SAINT);
  add_command("purge", do_purge, POSITION_DEAD, LOW_IMMORTAL);
  add_command("shutdown", do_shutdown, POSITION_DEAD, SILLYLORD);
  add_command("idea", do_idea, POSITION_DEAD, 0);
  add_command("typo", do_typo, POSITION_DEAD, 0);
  add_command("bug", do_bug, POSITION_DEAD, 0);
  add_command("whisper", do_whisper, POSITION_RESTING, 0);
  add_command("cast", do_cast, POSITION_SITTING, 1);
  add_command("at", do_at, POSITION_DEAD, CREATOR);
  add_command("ask", do_ask, POSITION_RESTING, 0);
  add_command("order", do_order, POSITION_RESTING, 1);
  add_command("sip", do_sip, POSITION_RESTING, 0);
  add_command("taste", do_taste, POSITION_RESTING, 0);
  add_command("snoop", do_snoop, POSITION_DEAD, GOD);
  add_command("follow", do_follow, POSITION_RESTING, 0);
  add_command("rent", do_not_here, POSITION_STANDING, 1);
  add_command("offer", do_not_here, POSITION_STANDING, 1);
  add_command("poke", do_action, POSITION_RESTING, 0);
  add_command("advance", do_advance, POSITION_DEAD, IMPLEMENTOR);
  add_command("accuse", do_action, POSITION_SITTING, 0);
  add_command("grin", do_action, POSITION_RESTING, 0);
  add_command("bow", do_action, POSITION_STANDING, 0);
  add_command("open", do_open, POSITION_SITTING, 0);
  add_command("close", do_close, POSITION_SITTING, 0);
  add_command("lock", do_lock, POSITION_SITTING, 0);
  add_command("unlock", do_unlock, POSITION_SITTING, 0);
  add_command("leave", do_leave, POSITION_STANDING, 0);
  add_command("applaud", do_action, POSITION_RESTING, 0);
  add_command("blush", do_action, POSITION_RESTING, 0);
  add_command("burp", do_action, POSITION_RESTING, 0);
  add_command("chuckle", do_action, POSITION_RESTING, 0);
  add_command("clap", do_action, POSITION_RESTING, 0);
  add_command("cough", do_action, POSITION_RESTING, 0);
  add_command("curtsey", do_action, POSITION_STANDING, 0);
  add_command("fart", do_action, POSITION_RESTING, 0);
  add_command("flip", do_action, POSITION_STANDING, 0);
  add_command("fondle", do_action, POSITION_RESTING, 0);
  add_command("frown", do_action, POSITION_RESTING, 0);
  add_command("gasp", do_action, POSITION_RESTING, 0);
  add_command("glare", do_action, POSITION_RESTING, 0);
  add_command("groan", do_action, POSITION_RESTING, 0);
  add_command("grope", do_action, POSITION_RESTING, 0);
  add_command("hiccup", do_action, POSITION_RESTING, 0);
  add_command("lick", do_action, POSITION_RESTING, 0);
  add_command("love", do_action, POSITION_RESTING, 0);
  add_command("moan", do_action, POSITION_RESTING, 0);
  add_command("nibble", do_action, POSITION_RESTING, 0);
  add_command("pout", do_action, POSITION_RESTING, 0);
  add_command("purr", do_action, POSITION_RESTING, 0);
  add_command("ruffle", do_action, POSITION_STANDING, 0);
  add_command("shiver", do_action, POSITION_RESTING, 0);
  add_command("shrug", do_action, POSITION_RESTING, 0);
  add_command("sing", do_action, POSITION_RESTING, 0);
  add_command("slap", do_action, POSITION_RESTING, 0);
  add_command("smirk", do_action, POSITION_RESTING, 0);
  add_command("snap", do_action, POSITION_RESTING, 0);
  add_command("sneeze", do_action, POSITION_RESTING, 0);
  add_command("snicker", do_action, POSITION_RESTING, 0);
  add_command("sniff", do_action, POSITION_RESTING, 0);
  add_command("snore", do_action, POSITION_SLEEPING, 0);
  add_command("spit", do_action, POSITION_STANDING, 0);
  add_command("squeeze", do_action, POSITION_RESTING, 0);
  add_command("stare", do_action, POSITION_RESTING, 0);
  add_command("strut", do_action, POSITION_STANDING, 0);
  add_command("thank", do_action, POSITION_RESTING, 0);
  add_command("twiddle", do_action, POSITION_RESTING, 0);
  add_command("wave", do_action, POSITION_RESTING, 0);
  add_command("whistle", do_action, POSITION_RESTING, 0);
  add_command("wiggle", do_action, POSITION_STANDING, 0);
  add_command("wink", do_action, POSITION_RESTING, 0);
  add_command("yawn", do_action, POSITION_RESTING, 0);
  add_command("snowball", do_action, POSITION_STANDING, DEMIGOD);
  add_command("write", do_write, POSITION_STANDING, 1);
  add_command("hold", do_grab, POSITION_RESTING, 1);
  add_command("flee", do_flee, POSITION_SITTING, 1);
  add_command("sneak", do_sneak, POSITION_STANDING, 1);
  add_command("hide", do_hide, POSITION_RESTING, 1);
  add_command("backstab", do_backstab, POSITION_STANDING, 1);
  add_command("pick", do_pick, POSITION_STANDING, 1);
  add_command("steal", do_steal, POSITION_STANDING, 1);
  add_command("bash", do_bash, POSITION_FIGHTING, 1);
  add_command("rescue", do_rescue, POSITION_FIGHTING, 1);
  add_command("kick", do_kick, POSITION_FIGHTING, 1);
  add_command("french", do_action, POSITION_RESTING, 0);
  add_command("comb", do_action, POSITION_RESTING, 0);
  add_command("massage", do_action, POSITION_RESTING, 0);
  add_command("tickle", do_action, POSITION_RESTING, 0);
  add_command("practice", do_practice, POSITION_RESTING, 1);
  add_command("pat", do_action, POSITION_RESTING, 0);
  add_command("examine", do_examine, POSITION_SITTING, 0);
  add_command("take", do_get, POSITION_RESTING, 1);
  add_command("info", do_info, POSITION_SLEEPING, 0);
  add_command("'", do_say, POSITION_RESTING, 0);
  add_command("curse", do_action, POSITION_RESTING, 0);
  add_command("use", do_use, POSITION_SITTING, 1);
  add_command("where", do_where, POSITION_DEAD, 1);
  add_command("levels", do_levels, POSITION_DEAD, 0);
  add_command("reroll", do_reroll, POSITION_DEAD, SILLYLORD);
  add_command("pray", do_action, POSITION_SITTING, 0);
  add_command(",", do_emote, POSITION_SLEEPING, 0);
  add_command("beg", do_action, POSITION_RESTING, 0);
  add_command("bleed", do_not_here, POSITION_RESTING, 0);
  add_command("cringe", do_action, POSITION_RESTING, 0);
  add_command("daydream", do_action, POSITION_SLEEPING, 0);
  add_command("fume", do_action, POSITION_RESTING, 0);
  add_command("grovel", do_action, POSITION_RESTING, 0);
  add_command("hop", do_action, POSITION_RESTING, 0);
  add_command("nudge", do_action, POSITION_RESTING, 0);
  add_command("peer", do_action, POSITION_RESTING, 0);
  add_command("point", do_action, POSITION_RESTING, 0);
  add_command("ponder", do_action, POSITION_RESTING, 0);
  add_command("punch", do_action, POSITION_RESTING, 0);
  add_command("snarl", do_action, POSITION_RESTING, 0);
  add_command("spank", do_action, POSITION_RESTING, 0);
  add_command("steam", do_action, POSITION_RESTING, 0);
  add_command("tackle", do_action, POSITION_RESTING, 0);
  add_command("taunt", do_action, POSITION_RESTING, 0);
  add_command("think", do_commune, POSITION_RESTING, LOW_IMMORTAL);
  add_command("whine", do_action, POSITION_RESTING, 0);
  add_command("worship", do_action, POSITION_RESTING, 0);
  add_command("yodel", do_action, POSITION_RESTING, 0);
  add_command("brief", do_brief, POSITION_DEAD, 0);
  add_command("wizlist", do_wizlist, POSITION_DEAD, 0);
  add_command("consider", do_consider, POSITION_RESTING, 0);
  add_command("group", do_group, POSITION_RESTING, 1);
  add_command("restore", do_restore, POSITION_DEAD, DEMIGOD);
  add_command("return", do_return, POSITION_DEAD, 0);
  add_command("switch", do_switch, POSITION_DEAD, 52);
  add_command("quaff", do_quaff, POSITION_RESTING, 0);
  add_command("recite", do_recite, POSITION_STANDING, 0);
  add_command("users", do_users, POSITION_DEAD, LOW_IMMORTAL);
  add_command("pose", do_pose, POSITION_STANDING, 0);
  add_command("noshout", do_noshout, POSITION_SLEEPING, LOW_IMMORTAL);
  add_command("wizhelp", do_wizhelp, POSITION_SLEEPING, LOW_IMMORTAL);
  add_command("credits", do_credits, POSITION_DEAD, 0);
  add_command("compact", do_compact, POSITION_DEAD, 0);
  add_command(":", do_emote, POSITION_SLEEPING, 0);
  add_command("deafen", do_plr_noshout, POSITION_SLEEPING, 1);
  add_command("slay", do_kill, POSITION_STANDING, SILLYLORD);
  add_command("wimpy", do_wimp, POSITION_DEAD, 0);
  add_command("junk", do_junk, POSITION_RESTING, 1);
  add_command("deposit", do_not_here, POSITION_RESTING, 1);
  add_command("withdraw", do_not_here, POSITION_RESTING, 1);
  add_command("balance", do_not_here, POSITION_RESTING, 1);
  add_command("nohassle", do_nohassle, POSITION_DEAD, LOW_IMMORTAL);
  add_command("system", do_system, POSITION_DEAD, SILLYLORD);
  add_command("pull", do_not_here, POSITION_STANDING, 1);
  add_command("stealth", do_stealth, POSITION_DEAD, LOW_IMMORTAL);
  add_command("edit", do_edit, POSITION_DEAD, CREATOR);
#ifdef TEST_SERVER
  add_command("@", do_set, POSITION_DEAD, CREATOR);
#else
  add_command("@", do_set, POSITION_DEAD, SILLYLORD);
#endif
  add_command("rsave", do_rsave, POSITION_DEAD, CREATOR);
  add_command("rload", do_rload, POSITION_DEAD, CREATOR);
  add_command("track", do_track, POSITION_DEAD, 1);
  add_command("wizlock", do_wizlock, POSITION_DEAD, DEMIGOD);
  add_command("highfive", do_highfive, POSITION_DEAD, 0);
  add_command("title", do_title, POSITION_DEAD, 43);
  add_command("whozone", do_who, POSITION_DEAD, 0);
  add_command("assist", do_assist, POSITION_FIGHTING, 1);
  add_command("attribute", do_attribute, POSITION_DEAD, 5);
  add_command("world", do_world, POSITION_DEAD, 0);
  add_command("allspells", do_spells, POSITION_DEAD, 0);
  add_command("breath", do_breath, POSITION_FIGHTING, 1);
  add_command("show", do_show, POSITION_DEAD, CREATOR);
  add_command("debug", do_debug, POSITION_DEAD, IMPLEMENTOR);
  add_command("invisible", do_invis, POSITION_DEAD, LOW_IMMORTAL);
  add_command("gain", do_gain, POSITION_DEAD, 1);
  add_command("instazone", do_instazone, POSITION_DEAD, CREATOR);
  add_command("disarm", do_disarm, POSITION_FIGHTING, 1);
  add_command("bonk", do_action, POSITION_SITTING, 1);
  add_command("chpwd", do_passwd, POSITION_SITTING, IMPLEMENTOR);
  add_command("fill", do_not_here, POSITION_SITTING, 0);
  add_command("imptest", do_doorbash, POSITION_SITTING, IMPLEMENTOR);
  add_command("shoot", do_shoot, POSITION_STANDING, 1);
  add_command("silence", do_silence, POSITION_STANDING, DEMIGOD);
  add_command("teams", do_not_here, POSITION_STANDING, LOKI);
  add_command("player", do_not_here, POSITION_STANDING, LOKI);
  add_command("create", do_create, POSITION_STANDING, GOD);
  add_command("bamfin", do_bamfin, POSITION_STANDING, LOW_IMMORTAL);
  add_command("bamfout", do_bamfout, POSITION_STANDING, LOW_IMMORTAL);
  add_command("vis", do_invis, POSITION_STANDING, 0);
  add_command("doorbash", do_doorbash, POSITION_STANDING, 1);
  add_command("mosh", do_action, POSITION_FIGHTING, 1);

/* alias commands */
  add_command("alias", do_alias, POSITION_SLEEPING, 1);
  add_command("1", do_alias, POSITION_DEAD, 1);
  add_command("2", do_alias, POSITION_DEAD, 1);
  add_command("3", do_alias, POSITION_DEAD, 1);
  add_command("4", do_alias, POSITION_DEAD, 1);
  add_command("5", do_alias, POSITION_DEAD, 1);
  add_command("6", do_alias, POSITION_DEAD, 1);
  add_command("7", do_alias, POSITION_DEAD, 1);
  add_command("8", do_alias, POSITION_DEAD, 1);
  add_command("9", do_alias, POSITION_DEAD, 1);
  add_command("0", do_alias, POSITION_DEAD, 1);
  add_command("swim", do_swim, POSITION_STANDING, 1);
  add_command("spy", do_spy, POSITION_STANDING, 1);
  add_command("springleap", do_springleap, POSITION_RESTING, 1);
  add_command("quivering palm", do_quivering_palm, POSITION_FIGHTING, 30);
  add_command("feign death", do_feign_death, POSITION_FIGHTING, 1);
  add_command("mount", do_mount, POSITION_STANDING, 1);
  add_command("dismount", do_mount, POSITION_MOUNTED, 1);
  add_command("ride", do_mount, POSITION_STANDING, 1);
  add_command("sign", do_sign, POSITION_RESTING, 1);
  add_command("setsev", do_setsev, POSITION_DEAD, IMMORTAL);
  add_command("first aid", do_first_aid, POSITION_RESTING, 1);
  add_command("log", do_set_log, POSITION_DEAD, 58);
  add_command("recall", do_cast, POSITION_DEAD, LOKI);
  add_command_dep("reload", reboot_text, 284, POSITION_DEAD, 57);
  add_command("event", do_event, POSITION_DEAD, 59);
  add_command("disguise", do_disguise, POSITION_STANDING, 1);
  add_command("climb", do_climb, POSITION_STANDING, 1);
  add_command("beep", do_beep, POSITION_DEAD, 51);
  add_command("bite", do_bite, POSITION_RESTING, 1);
  add_command("redit", do_redit, POSITION_SLEEPING, CREATOR);
  add_command("display", do_display, POSITION_SLEEPING, 1);
  add_command("resize", do_resize, POSITION_SLEEPING, 1);
  add_command("\"", do_commune, POSITION_SLEEPING, LOW_IMMORTAL);
  add_command("#", do_cset, POSITION_DEAD, 59);
  add_command("inset", do_inset, POSITION_RESTING, 1);
  add_command("showexits", do_show_exits, POSITION_DEAD, 1);
  add_command("split", do_split, POSITION_RESTING, 1);
  add_command("report", do_report, POSITION_RESTING, 1);
  add_command("gname", do_gname, POSITION_RESTING, 1);
#if STUPID
  /* this command is a little flawed.  Heavy usage generates obscenely 
     long linked lists in the "donation room" which cause the mud to 
     lag a horrible death. */
  add_command("donate", do_donate, POSITION_STANDING, 1);
#endif
  add_command("auto", do_auto, POSITION_RESTING, 1);
  add_command("brew", do_makepotion, POSITION_RESTING, 1);
  add_command("changeform", do_changeform, POSITION_STANDING, 1);
  add_command("walk", do_walk, POSITION_STANDING, 1);
  add_command("fly", do_fly, POSITION_STANDING, 1);
  add_command("berserk", do_berserk, POSITION_FIGHTING, 1);
  add_command("palm", do_palm, POSITION_STANDING, 1);
  add_command("peek", do_peek, POSITION_STANDING, 1);
  add_command("prompt", do_prompt, POSITION_RESTING, 1);
#if PLAYER_AUTH
  add_command("auth", do_auth, POSITION_SLEEPING, LOW_IMMORTAL);
#endif
}


/* *************************************************************************
 *  Stuff for controlling the non-playing sockets (get name, pwd etc)       *
 ************************************************************************* */




/* locate entry in p_table with entry->name == name. -1 mrks failed search */
int find_name(char *name) {
  int i;

  for (i = 0; i <= top_of_p_table; i++) {
    if (!str_cmp((player_table + i)->name, name))
      return (i);
  }

  return (-1);
}



int _parse_name(char *arg, char *name) {
  int i;

  /* skip whitespaces */
  for (; isspace(*arg); arg++);

  for (i = 0; (*name = *arg) != '\0'; arg++, i++, name++)
    if ((*arg < 0) || !isalpha(*arg) || i > 15)
      return (1);

  if (!i)
    return (1);

  return (0);
}





/* deal with newcomers and other non-playing sockets */
void nanny(struct descriptor_data *d, char *arg) {

  char buf[100], *help_thing;
  int player_i, count = 0, oops = FALSE, index = 0, number, choice;
  int i;
  int junk[6];                  /* generic counter */
  char tmp_name[20];
  bool koshername;
  struct char_file_u tmp_store;
  struct char_data *tmp_ch;
  struct descriptor_data *k;
  extern struct descriptor_data *descriptor_list;
  extern int WizLock;
  extern int plr_tick_count;
  extern int top_of_mobt;
  extern int RacialMax[][6];

  write(d->descriptor, echo_on, 6);

  switch (STATE(d)) {

  case CON_ALIGN:

    for (; isspace(*arg); arg++);

    if (!arg) {
      if (!(GET_ALIGNMENT(d->character)))
        GET_ALIGNMENT(d->character) = get_racial_alignment(d);
      if (!(GET_ALIGNMENT(d->character))) {
        SEND_TO_Q
          ("Shall you start with Good, Neutral, or Evil (G/N/E) tendencies? ",
           d);
        STATE(d) = CON_ALIGN;
        break;
      }
      else {                    /* We aren't neutral anyway, skip this part */
        if (GET_RACE(d->character) == RACE_VEGMAN) {
          SEND_TO_Q(VT_HOMECLR, d);
          SEND_TO_Q("\n\rVeggies have no gender.\n\r", d);
          d->character->player.sex = SEX_NEUTRAL;
          SEND_TO_Q(STATQ_MESSG, d);
          STATE(d) = CON_STAT_LIST;
          return;
        }
        SEND_TO_Q("What is your gender, male or female (M/F)?", d);
        STATE(d) = CON_QSEX;
        break;
      }
    }
    else if (!(GET_ALIGNMENT(d->character))) {
      if (!strcmp(arg, "G") || !strcmp(arg, "g")) {
        GET_ALIGNMENT(d->character) = 500;
        SEND_TO_Q("You will enter the realms a champion of goodness.", d);
        if (GET_RACE(d->character) == RACE_VEGMAN) {
          SEND_TO_Q(VT_HOMECLR, d);
          SEND_TO_Q("\n\rVeggies have no gender.\n\r", d);
          d->character->player.sex = SEX_NEUTRAL;
          SEND_TO_Q(STATQ_MESSG, d);
          STATE(d) = CON_STAT_LIST;
          return;
        }
        SEND_TO_Q("\n\r\n\rWhat is your gender, male or female (M/F)?", d);
        STATE(d) = CON_QSEX;
        break;
      }
      else if (!strcmp(arg, "N") || !strcmp(arg, "n")) {
        GET_ALIGNMENT(d->character) = 0;
        SEND_TO_Q("You will enter the realms unbiased.", d);
        if (GET_RACE(d->character) == RACE_VEGMAN) {
          SEND_TO_Q(VT_HOMECLR, d);
          SEND_TO_Q("\n\rVeggies have no gender.\n\r", d);
          d->character->player.sex = SEX_NEUTRAL;
          SEND_TO_Q(STATQ_MESSG, d);
          STATE(d) = CON_STAT_LIST;
          return;
        }
        SEND_TO_Q("\n\r\n\rWhat is your gender, male or female (M/F)?", d);
        STATE(d) = CON_QSEX;
        break;
      }
      else if (!strcmp(arg, "E") || !strcmp(arg, "e")) {
        GET_ALIGNMENT(d->character) = -500;
        SEND_TO_Q("You will enter the realms as a minion of evil.", d);
        if (GET_RACE(d->character) == RACE_VEGMAN) {
          SEND_TO_Q(VT_HOMECLR, d);
          SEND_TO_Q("\n\rVeggies have no gender.\n\r", d);
          d->character->player.sex = SEX_NEUTRAL;
          SEND_TO_Q(STATQ_MESSG, d);
          STATE(d) = CON_STAT_LIST;
          return;
        }
        SEND_TO_Q("\n\r\n\rWhat is your gender, male or female (M/F)?", d);
        STATE(d) = CON_QSEX;
        break;
      }
      else {
        SEND_TO_Q("Please enter (G/N/E) to describe your tendencies: ", d);
        STATE(d) = CON_ALIGN;
        break;
      }
    }
    else {                      /* railroaded into an alignment */
      if (GET_RACE(d->character) == RACE_VEGMAN) {
        SEND_TO_Q(VT_HOMECLR, d);
        SEND_TO_Q("\n\rVeggies have no gender.\n\r", d);
        d->character->player.sex = SEX_NEUTRAL;
        SEND_TO_Q(STATQ_MESSG, d);
        STATE(d) = CON_STAT_LIST;
        return;
      }
      SEND_TO_Q("What is your gender, male or female (M/F)?", d);
      STATE(d) = CON_QSEX;
      break;
    }
    break;
  case CON_QRACE:

    for (; isspace(*arg); arg++);
    if (!*arg) {
      SEND_TO_Q(VT_HOMECLR, d);
      SEND_TO_Q("Choose A Race:\n\r\n\r", d);
      display_races(d);
      STATE(d) = CON_QRACE;
    }
    else {
      if (is_number(arg)) {
        choice = atoi(arg);
      }
      else if (*arg == '?') {
        /*         SEND_TO_Q(RACEHELP, d); */
        arg++;                  /* increment past the ? */
        for (; isspace(*arg); arg++);   /* eat more spaces if any */
        if (is_number(arg)) {
          choice = atoi(arg);
          for (i = 1; RaceList[i].what[0] != '\n'; i++);
          if (choice < 1 || choice > i) {
            SEND_TO_Q("That is not a valid race number.\n\r", d);
            SEND_TO_Q("\n\r*** PRESS RETURN ***", d);
            STATE(d) = CON_QRACE;
            break;
          }
          else {
            extern struct help_index_element *help_index;

            if (!help_index) {
              SEND_TO_Q("Sorry, no help is currently available.\n\r", d);
              STATE(d) = CON_QRACE;
              break;
            }
            choice--;
            help_thing = RaceList[choice].what;
            GET_RACE(d->character) = RaceList[choice].race_num; /* for CVC() */
            if (*help_thing == '*')     /* infra types */
              ++help_thing;
            SEND_TO_Q(VT_HOMECLR, d);
            do_help(d->character, help_thing, 0);
            SEND_TO_Q("Level limits (NVC = Not a Valid Class):\n\r", d);
            if (check_valid_class(d, CLASS_MAGIC_USER)) {
              SPRINTF(buf, "Mage: %d  ",
                      RacialMax[RaceList[choice].race_num][0]);
              SEND_TO_Q(buf, d);
            }
            else
              SEND_TO_Q("Mage: NVC  ", d);

            if (check_valid_class(d, CLASS_CLERIC)) {
              SPRINTF(buf, "Cleric: %d  ",
                      RacialMax[RaceList[choice].race_num][1]);
              SEND_TO_Q(buf, d);
            }
            else
              SEND_TO_Q("Cleric: NVC  ", d);


            if (check_valid_class(d, CLASS_WARRIOR)) {
              SPRINTF(buf, "Warrior: %d  ",
                      RacialMax[RaceList[choice].race_num][2]);
              SEND_TO_Q(buf, d);
            }
            else
              SEND_TO_Q("Warrior: NVC  ", d);


            if (check_valid_class(d, CLASS_THIEF)) {
              SPRINTF(buf, "Thief: %d  ",
                      RacialMax[RaceList[choice].race_num][3]);
              SEND_TO_Q(buf, d);
            }
            else
              SEND_TO_Q("Thief: NVC  ", d);

            if (check_valid_class(d, CLASS_DRUID)) {
              SPRINTF(buf, "Druid: %d  ",
                      RacialMax[RaceList[choice].race_num][4]);
              SEND_TO_Q(buf, d);
            }
            else
              SEND_TO_Q("Druid: NVC  ", d);


            if (check_valid_class(d, CLASS_MONK)) {
              SPRINTF(buf, "Monk: %d  ",
                      RacialMax[RaceList[choice].race_num][0]);
              SEND_TO_Q(buf, d);
            }
            else
              SEND_TO_Q("Monk: NVC  ", d);


            SEND_TO_Q("\n\r", d);
            SEND_TO_Q("\n\r*** PRESS RETURN ***", d);
            STATE(d) = CON_QRACE;
            break;
          }
        }
        else {
          SEND_TO_Q("\n\rThat is not a valid choice!!", d);
          SEND_TO_Q("\n\r*** PRESS RETURN ***", d);
          STATE(d) = CON_QRACE;
          break;
        }
      }
      else {
        SEND_TO_Q("\n\rThat is not a valid choice!!", d);
        SEND_TO_Q("\n\r*** PRESS RETURN ***", d);
        STATE(d) = CON_QRACE;
        break;
      }
      /* assume a race number was chosen */
      for (i = 1; RaceList[i].what[0] != '\n'; i++);
      if (choice < 1 || choice > i) {
        SEND_TO_Q("That is not a valid race number.\n\r", d);
        SEND_TO_Q("\n\r*** PRESS RETURN ***", d);
        STATE(d) = CON_QRACE;
        break;
      }
      else {
        GET_RACE(d->character) = RaceList[choice - 1].race_num;
        if (check_valid_class(d, CLASS_DRUID))
          SEND_TO_Q("Reminder:  Druids must be neutral.\n\r", d);
        if (!get_racial_alignment(d)) { /* neutral chars can choose */
          SEND_TO_Q
            ("Shall you start with Good, Neutral, or Evil (G/N/E) tendencies? ",
             d);
          STATE(d) = CON_ALIGN;
          break;
        }
        else {
          GET_ALIGNMENT(d->character) = get_racial_alignment(d);
          if (GET_ALIGNMENT(d->character) < 0)
            SEND_TO_Q("You are now a minion of evil.\n\r", d);
          else
            SEND_TO_Q("You are now a champion of goodness.\n\r", d);
          if (GET_RACE(d->character) == RACE_VEGMAN) {
            SEND_TO_Q(VT_HOMECLR, d);
            SEND_TO_Q("\n\rVeggies have no gender.\n\r", d);
            d->character->player.sex = SEX_NEUTRAL;
            SEND_TO_Q(STATQ_MESSG, d);
            STATE(d) = CON_STAT_LIST;
            return;
          }
          SEND_TO_Q("What is your gender, male or female (M/F)?", d);
          STATE(d) = CON_QSEX;
          break;

        }
      }
    }

    break;

  case CON_NME:                /* wait for input of name       */
    if (!d->character) {
      CREATE(d->character, struct char_data, 1);
      clear_char(d->character);
      d->character->desc = d;
    }

    for (; isspace(*arg); arg++);
    if (!*arg)
      close_socket(d);
    else {

      if (_parse_name(arg, tmp_name)) {
        SEND_TO_Q("Illegal name, please try another.", d);
        SEND_TO_Q("Name: ", d);
        return;
      }
/*
  i would like to begin here, and explain the concept of the
  bicameral democratic system.


   NOT! :-)
*/

      if (!strncmp(d->host, "128.197.152.10", 14)) {
        if (!strcmp(tmp_name, "Kitten") || !strcmp(tmp_name, "SexKitten")
            || !strcmp(tmp_name, "Rugrat")) {
          SEND_TO_Q("You are a special exception.\n\r", d);
        }
        else {
          SEND_TO_Q("Sorry, this site is temporarily banned.\n\r", d);
          close_socket(d);
        }
      }
      /* Check if already playing */
      for (k = descriptor_list; k; k = k->next) {
        if ((k->character != d->character) && k->character) {
          if (k->original) {
            if (GET_NAME(k->original) &&
                (str_cmp(GET_NAME(k->original), tmp_name) == 0)) {
              SEND_TO_Q("Already playing, cannot connect\n\r", d);
              SEND_TO_Q("Name: ", d);
              return;
            }
          }
          else {                /* No switch has been made */
            if (GET_NAME(k->character) &&
                (str_cmp(GET_NAME(k->character), tmp_name) == 0)) {
              SEND_TO_Q("Already playing, cannot connect\n\r", d);
              SEND_TO_Q("Name: ", d);
              return;
            }
          }
        }
      }

      if ((player_i = load_char(tmp_name, &tmp_store)) > -1) {
        /*
         *  check for tmp_store.max_corpse;
         */
        /*
           if (tmp_store.max_corpse > 3) {
           SEND_TO_Q("Too many corpses in game, can not connect\n\r", d);
           SPRINTF(buf, "%s: too many corpses.",tmp_name);
           log_msg(buf);
           STATE(d) = CON_WIZLOCK;
           break;
           }
         */
        store_to_char(&tmp_store, d->character);
        strcpy(d->pwd, tmp_store.pwd);
        d->pos = player_table[player_i].nr;
        SEND_TO_Q("Password: ", d);
        write(d->descriptor, echo_off, 4);
        STATE(d) = CON_PWDNRM;
      }
      else {
        koshername = TRUE;

        for (number = 0; number <= top_of_mobt; number++) {
          if (isname(tmp_name, mob_index[number].name)) {
            koshername = FALSE;
            break;
          }
        }
        if (koshername == FALSE) {
          SEND_TO_Q("You have chosen a name in use by a monster.\n\r", d);
          SEND_TO_Q("For your own safety, choose another name: ", d);
          return;
        }

        /* player unknown gotta make a new */
        if (!WizLock) {
          CREATE(GET_NAME(d->character), char, strlen(tmp_name) + 1);
          strcpy(GET_NAME(d->character), CAP(tmp_name));
          SPRINTF(buf, "Did I get that right, %s (Y/N)? ", tmp_name);
          SEND_TO_Q(buf, d);
          STATE(d) = CON_NMECNF;
        }
        else {
          SPRINTF(buf, "Sorry, no new characters at this time\n\r");
          SEND_TO_Q(buf, d);
          STATE(d) = CON_WIZLOCK;
        }
      }
    }
    break;

  case CON_NMECNF:             /* wait for conf. of new name   */
    /* skip whitespaces */
    for (; isspace(*arg); arg++);

    if (*arg == 'y' || *arg == 'Y') {
      SEND_TO_Q("New character.\n\r", d);

      SPRINTF(buf, "Give me a password for %s: ", GET_NAME(d->character));

      SEND_TO_Q(buf, d);
      write(d->descriptor, echo_off, 4);
      STATE(d) = CON_PWDGET;
    }
    else {
      if (*arg == 'n' || *arg == 'N') {
        SEND_TO_Q("Ok, what IS it, then? ", d);
        free(GET_NAME(d->character));
        STATE(d) = CON_NME;
      }
      else {                    /* Please do Y or N */
        SEND_TO_Q("Please type Yes or No? ", d);
      }
    }
    break;

  case CON_PWDNRM:             /* get pwd for known player     */
    /* skip whitespaces */
    for (; isspace(*arg); arg++);
    if (!*arg)
      close_socket(d);
    else {
      if (strncmp(crypt(arg, d->character->player.name), d->pwd, 10)) {
        SEND_TO_Q("Wrong password.\n\r", d);
        close_socket(d);
        return;
      }
#if IMPL_SECURITY
      if (top_of_p_table > 0) {
        if (get_max_level(d->character) >= 58) {
          switch (sec_check(GET_NAME(d->character), d->host)) {
          case -1:
          case 0:
            SEND_TO_Q("Security check reveals invalid site\n\r", d);
            SEND_TO_Q("Speak to an implementor to fix problem\n\r", d);
            SEND_TO_Q("If you are an implementor, add yourself to the\n\r", d);
            SEND_TO_Q("Security directory (lib/security)\n\r", d);
            close_socket(d);
            break;
          }
        }
        else {
        }
      }
#endif

      for (tmp_ch = character_list; tmp_ch; tmp_ch = tmp_ch->next)
        if ((!str_cmp(GET_NAME(d->character), GET_NAME(tmp_ch)) &&
             !tmp_ch->desc && !IS_NPC(tmp_ch)) ||
            (IS_NPC(tmp_ch) && tmp_ch->orig &&
             !str_cmp(GET_NAME(d->character), GET_NAME(tmp_ch->orig)))) {

          write(d->descriptor, echo_on, 6);
          SEND_TO_Q("Reconnecting.\n\r", d);

          free_char(d->character);
          tmp_ch->desc = d;
          d->character = tmp_ch;
          tmp_ch->specials.timer = 0;
          if (!IS_IMMORTAL(tmp_ch)) {
            tmp_ch->invis_level = 0;

          }
          if (tmp_ch->orig) {
            tmp_ch->desc->original = tmp_ch->orig;
            tmp_ch->orig = 0;
          }
          d->character->persist = 0;
          STATE(d) = CON_PLYNG;

          act("$n has reconnected.", TRUE, tmp_ch, 0, 0, TO_ROOM);
          SPRINTF(buf, "%s[%s] has reconnected.",
                  GET_NAME(d->character), d->host);
          log_msg(buf);
          return;
        }


      SPRINTF(buf, "%s[%s] has connected.", GET_NAME(d->character), d->host);
      log_msg(buf);
      SEND_TO_Q(motd, d);
      SEND_TO_Q("\n\r\n*** PRESS RETURN: ", d);

      STATE(d) = CON_RMOTD;
    }
    break;

  case CON_PWDGET:             /* get pwd for new player       */
    /* skip whitespaces */
    for (; isspace(*arg); arg++);

    if (!*arg || strlen(arg) > 10) {

      write(d->descriptor, echo_on, 6);
      SEND_TO_Q("Illegal password.\n\r", d);
      SEND_TO_Q("Password: ", d);

      write(d->descriptor, echo_off, 4);
      return;
    }

    strncpy(d->pwd, crypt(arg, d->character->player.name), 10);
    *(d->pwd + 10) = '\0';
    write(d->descriptor, echo_on, 6);
    SEND_TO_Q("Please retype password: ", d);
    write(d->descriptor, echo_off, 4);
    STATE(d) = CON_PWDCNF;
    break;

  case CON_PWDCNF:             /* get confirmation of new pwd  */
    /* skip whitespaces */
    for (; isspace(*arg); arg++);

    if (strncmp(crypt(arg, d->character->player.name), d->pwd, 10)) {
      write(d->descriptor, echo_on, 6);

      SEND_TO_Q("Passwords do not match.\n\r", d);
      SEND_TO_Q("Retype password: ", d);
      STATE(d) = CON_PWDGET;
      write(d->descriptor, echo_off, 4);
      return;
    }
    else {
      write(d->descriptor, echo_on, 6);

      SEND_TO_Q("Choose A Race:\n\r\n\r", d);
      display_races(d);
      STATE(d) = CON_QRACE;
    }
    break;

  case CON_QSEX:               /* query sex of new user        */

    for (; isspace(*arg); arg++);
    switch (*arg) {
    case 'm':
    case 'M':
      /* sex MALE */
      d->character->player.sex = SEX_MALE;
      break;

    case 'f':
    case 'F':
      /* sex FEMALE */
      d->character->player.sex = SEX_FEMALE;
      break;

    default:
      SEND_TO_Q("That is not a valid gender type!\n\r", d);
      SEND_TO_Q("What IS your gender, male or female (M/F)?", d);
      return;
      break;
    }
    SEND_TO_Q(VT_HOMECLR, d);
    SEND_TO_Q(STATQ_MESSG, d);
    STATE(d) = CON_STAT_LIST;
    break;

  case CON_STAT_LIST:
    /* skip whitespaces */

    index = 0;
    for (i = 0; i < 6; i++)
      junk[i] = 0;

    while (*arg && index < MAX_STAT) {
      for (; isspace(*arg); arg++);
      if (*arg == 'S' || *arg == 's') {
        if (!junk[0])
          d->stat[index++] = 's';
        junk[0]++;
      }
      else if (*arg == 'I' || *arg == 'i') {
        if (!junk[1])
          d->stat[index++] = 'i';
        junk[1]++;
      }
      else if (*arg == 'W' || *arg == 'w') {
        if (!junk[2])
          d->stat[index++] = 'w';
        junk[2]++;
      }
      else if (*arg == 'D' || *arg == 'd') {
        if (!junk[3])
          d->stat[index++] = 'd';
        junk[3]++;
      }
      else if (*arg == 'C' || *arg == 'c') {
        arg++;
        if (*arg == 'O' || *arg == 'o') {
          if (!junk[4])
            d->stat[index++] = 'o';
          junk[4]++;
        }
        else if (*arg == 'H' || *arg == 'h') {
          if (!junk[5])
            d->stat[index++] = 'h';
          junk[5]++;
        }
        else {
          SEND_TO_Q(VT_HOMECLR, d);
          SEND_TO_Q("That was an invalid choice.\n\r", d);
          SEND_TO_Q(STATQ_MESSG, d);
          STATE(d) = CON_STAT_LIST;
          break;
        }
      }
      else if (*arg == '?') {
        SEND_TO_Q(VT_HOMECLR, d);
        SEND_TO_Q(STATHELP, d);
        SEND_TO_Q(STATQ_MESSG, d);
        return;
        break;
      }
      else {
        SEND_TO_Q(VT_HOMECLR, d);
        SPRINTF(buf, "Hey, what kinda statistic does an %c represent?\n\r",
                *arg);
        SEND_TO_Q(STATQ_MESSG, d);
        STATE(d) = CON_STAT_LIST;
        return;
        break;
      }
      arg++;
    }
    if (index < MAX_STAT) {
      SEND_TO_Q(VT_HOMECLR, d);
      SEND_TO_Q("You did not enter enough legal statistics.\n\r", d);
      SEND_TO_Q(STATQ_MESSG, d);
      STATE(d) = CON_STAT_LIST;
      break;
    }
    else {
      SEND_TO_Q("Ok.. all chosen.\n\r", d);
      SEND_TO_Q("\n\r", d);
      display_race_classes(d);
#if PLAYER_AUTH
      /* set the AUTH flags */
      /* (3 chances) */
      d->character->generic = NEWBIE_REQUEST + NEWBIE_CHANCES;
#endif
      STATE(d) = CON_QCLASS;
      break;
    }
    display_race_classes(d);

#if PLAYER_AUTH
    /* set the AUTH flags */
    /* (3 chances) */
    d->character->generic = NEWBIE_REQUEST + NEWBIE_CHANCES;
#endif
    STATE(d) = CON_QCLASS;
    break;

  case CON_QCLASS:{
      /* skip whitespaces */
      for (; isspace(*arg); arg++);
      d->character->player.class = 0;
      count = 0;
      oops = FALSE;
      for (; *arg && count < 3 && !oops; arg++) {
        if (count && GET_RACE(d->character) == RACE_HUMANTWO)
          break;
        switch (*arg) {
        case 'm':
        case 'M':{
            if (check_valid_class(d, CLASS_MAGIC_USER)) {
              if (!IS_SET(d->character->player.class, CLASS_MAGIC_USER))
                d->character->player.class += CLASS_MAGIC_USER;
              count++;
              STATE(d) = CON_RMOTD;
            }
            else {
              SEND_TO_Q(VT_HOMECLR, d);
              SEND_TO_Q("Your race may not be a magic user.\n\r", d);
              display_race_classes(d);
              STATE(d) = CON_QCLASS;
              return;
            }
            break;
          }
        case 'c':
        case 'C':{
            if (check_valid_class(d, CLASS_CLERIC)) {
              if (!IS_SET(d->character->player.class, CLASS_CLERIC))
                d->character->player.class += CLASS_CLERIC;
              count++;
              STATE(d) = CON_RMOTD;
            }
            else {
              SEND_TO_Q(VT_HOMECLR, d);
              SEND_TO_Q("Your race may not be a cleric.\n\r", d);
              display_race_classes(d);
              STATE(d) = CON_QCLASS;
              return;

            }
            break;
          }
        case 'k':
        case 'K':{
            if (check_valid_class(d, CLASS_MONK)) {
              if (!IS_SET(d->character->player.class, CLASS_MONK)) {
                d->character->player.class = CLASS_MONK;
                count = 4;
              }
              count++;
              STATE(d) = CON_RMOTD;
            }
            else {
              SEND_TO_Q(VT_HOMECLR, d);
              SEND_TO_Q("Only humans may be monks.\n\r", d);
              display_race_classes(d);
              STATE(d) = CON_QCLASS;
              return;
            }
            break;
          }
        case 'd':
        case 'D':{
            if (check_valid_class(d, CLASS_DRUID)) {
              if (!IS_SET(d->character->player.class, CLASS_DRUID))
                d->character->player.class += CLASS_DRUID;
              count++;
              STATE(d) = CON_RMOTD;
            }
            else {
              SEND_TO_Q(VT_HOMECLR, d);
              SEND_TO_Q("Your race may not be druids.\n\r", d);
              display_race_classes(d);
              STATE(d) = CON_QCLASS;
              return;
            }
            break;
          }
        case 'f':
        case 'F':
        case 'w':
        case 'W':{
            if (check_valid_class(d, CLASS_WARRIOR)) {
              if (!IS_SET(d->character->player.class, CLASS_WARRIOR))
                d->character->player.class += CLASS_WARRIOR;
              count++;
              STATE(d) = CON_RMOTD;
            }
            else {
              SEND_TO_Q(VT_HOMECLR, d);
              SEND_TO_Q("Your race may not be warriors.\n\r", d);
              display_race_classes(d);
              STATE(d) = CON_QCLASS;
              return;
            }
            break;
          }
        case 't':
        case 'T':{
            if (check_valid_class(d, CLASS_THIEF)) {
              if (!IS_SET(d->character->player.class, CLASS_THIEF))
                d->character->player.class += CLASS_THIEF;
              count++;
              STATE(d) = CON_RMOTD;
            }
            else {
              SEND_TO_Q(VT_HOMECLR, d);
              SEND_TO_Q("Your race may not be a thieves.\n\r", d);
              display_race_classes(d);
              STATE(d) = CON_QCLASS;
              return;
            }
            break;
          }
        case '\\':             /* ignore these */
        case '/':
          break;

        case '?':
          SEND_TO_Q("Cleric:       Good defense.  Healing spells\n\r", d);
          SEND_TO_Q
            ("Druid:        Real outdoors types.  Spells, not many items\n\r",
             d);
          SEND_TO_Q("Fighter:      Big, strong and stupid.  Nuff said.\n\r",
                    d);
          SEND_TO_Q
            ("Magic-users:  Weak, puny, smart and very powerful at high levels.\n\r",
             d);
          SEND_TO_Q
            ("Thieves:      Quick, agile, sneaky.  Nobody trusts them.\n\r",
             d);
          SEND_TO_Q
            ("Monks:        Masters of the martial arts.  They can only be single classed\n\r",
             d);
          SEND_TO_Q("\n\r", d);
          display_race_classes(d);
          STATE(d) = CON_QCLASS;
          return;
          break;

        default:
          SEND_TO_Q("I do not recognize that class.\n\r", d);
          STATE(d) = CON_QCLASS;
          oops = TRUE;
          break;
        }
      }
      if (count == 0) {
        SEND_TO_Q("You must choose at least one class!\n\r", d);
        SEND_TO_Q("\n\r", d);
        display_race_classes(d);
        STATE(d) = CON_QCLASS;
        break;
      }
      else {

#if PLAYER_AUTH
        STATE(d) = CON_AUTH;
        SEND_TO_Q("***PRESS ENTER**", d);
#else
        if (STATE(d) != CON_QCLASS) {
          SPRINTF(buf, "%s [%s] new player.", GET_NAME(d->character), d->host);
          log_msg(buf);
          /*
           ** now that classes are set, initialize
           */
          init_char(d->character);
          /* create an entry in the file */
          d->pos = create_entry(GET_NAME(d->character));
          save_char(d->character, AUTO_RENT);
          SEND_TO_Q(motd, d);
          SEND_TO_Q("\n\r\n*** PRESS RETURN: ", d);
          STATE(d) = CON_RMOTD;
        }
#endif
      }
    }
    break;


#if PLAYER_AUTH
  case CON_AUTH:{              /* notify gods */
      if (d->character->generic >= NEWBIE_START) {
        /*
         ** now that classes are set, initialize
         */
        init_char(d->character);
        /* create an entry in the file */
        d->pos = create_entry(GET_NAME(d->character));
        save_char(d->character, AUTO_RENT);
        SEND_TO_Q(motd, d);
        SEND_TO_Q("\n\r\n*** PRESS RETURN: ", d);
        STATE(d) = CON_RMOTD;
      }
      else if (d->character->generic >= NEWBIE_REQUEST) {
        SPRINTF(buf, "%s [%s] new player.", GET_NAME(d->character), d->host);
        log_sev(buf, 7);
        if (!strncmp(d->host, "128.197.152", 11))
          d->character->generic = 1;
        /* I decided to give them another chance.  -Steppenwolf  */
        /* They blew it. -DM */
        if (!strncmp(d->host, "oak.grove", 9)
            || !strncmp(d->host, "143.195.1.20", 12)) {
          d->character->generic = 1;
        }
        else {
          if (top_of_p_table > 0) {
            SPRINTF(buf, "type Auth[orize] %s to allow into game.",
                    GET_NAME(d->character));
            log_sev(buf, 6);
            log_sev("type 'Help Authorize' for other commands", 2);
          }
          else {
            log_msg("Initial character.  Authorized Automatically");
            d->character->generic = NEWBIE_START + 5;
          }
        }
        /*
         **  enough for gods.  now player is told to shut up.
         */
        d->character->generic--;        /* NEWBIE_START == 3 == 3 chances */
        SPRINTF(buf, "Please wait. You have %d requests remaining.\n\r",
                d->character->generic);
        SEND_TO_Q(buf, d);
        if (d->character->generic == 0) {
          SEND_TO_Q("Goodbye.", d);
          STATE(d) = CON_WIZLOCK;       /* axe them */
          break;
        }
        else {
          SEND_TO_Q("Please Wait.\n\r", d);
          STATE(d) = CON_AUTH;
        }
      }
      else {                    /* Axe them */
        STATE(d) = CON_WIZLOCK;
      }
    }
    break;
#endif

  case CON_RMOTD:              /* read CR after printing motd  */
    if (get_max_level(d->character) > 50) {
      SEND_TO_Q(wmotd, d);
      SEND_TO_Q("\n\r\n[PRESS RETURN]", d);
      STATE(d) = CON_WMOTD;
      break;
    }
    if (d->character->term != 0)
      screen_off(d->character);
    SEND_TO_Q(MENU, d);
    STATE(d) = CON_SLCT;
    if (WizLock) {
      if (get_max_level(d->character) < LOW_IMMORTAL) {
        SPRINTF(buf, "Sorry, the game is locked up for repair\n\r");
        SEND_TO_Q(buf, d);
        STATE(d) = CON_WIZLOCK;
      }
    }
    break;


  case CON_WMOTD:              /* read CR after printing motd  */

    SEND_TO_Q(MENU, d);
    STATE(d) = CON_SLCT;
    if (WizLock) {
      if (get_max_level(d->character) < LOW_IMMORTAL) {
        SPRINTF(buf, "Sorry, the game is locked up for repair\n\r");
        SEND_TO_Q(buf, d);
        STATE(d) = CON_WIZLOCK;
      }
    }
    break;

  case CON_WIZLOCK:
    close_socket(d);
    break;

  case CON_CITY_CHOICE:
    /* skip whitespaces */
    for (; isspace(*arg); arg++);
    if (d->character->in_room != NOWHERE) {
      SEND_TO_Q("This choice is only valid when you have been auto-saved\n\r",
                d);
      STATE(d) = CON_SLCT;
    }
    else {
      switch (*arg) {
      case '1':

        reset_char(d->character);
        SPRINTF(buf, "Loading %s's equipment", d->character->player.name);
        log_msg(buf);
        load_char_objs(d->character);
        save_char(d->character, AUTO_RENT);
        send_to_char(WELC_MESSG, d->character);
        d->character->next = character_list;
        character_list = d->character;

        char_to_room(d->character, 3001);
        d->character->player.hometown = 3001;


        d->character->specials.tick = plr_tick_count++;
        if (plr_tick_count == PLR_TICK_WRAP)
          plr_tick_count = 0;

        act("$n has entered the game.", TRUE, d->character, 0, 0, TO_ROOM);
        STATE(d) = CON_PLYNG;
        if (!get_max_level(d->character))
          do_start(d->character);
        do_look(d->character, "", "look");
        d->prompt_mode = 1;

        break;
      case '2':

        reset_char(d->character);
        SPRINTF(buf, "Loading %s's equipment", d->character->player.name);
        log_msg(buf);
        load_char_objs(d->character);
        save_char(d->character, AUTO_RENT);
        send_to_char(WELC_MESSG, d->character);
        d->character->next = character_list;
        character_list = d->character;

        char_to_room(d->character, 1103);
        d->character->player.hometown = 1103;

        d->character->specials.tick = plr_tick_count++;
        if (plr_tick_count == PLR_TICK_WRAP)
          plr_tick_count = 0;

        act("$n has entered the game.", TRUE, d->character, 0, 0, TO_ROOM);
        STATE(d) = CON_PLYNG;
        if (!get_max_level(d->character))
          do_start(d->character);
        do_look(d->character, "", "look");
        d->prompt_mode = 1;

        break;
      case '3':
        if (get_max_level(d->character) > 5) {

          reset_char(d->character);
          SPRINTF(buf, "Loading %s's equipment", d->character->player.name);
          log_msg(buf);
          load_char_objs(d->character);
          save_char(d->character, AUTO_RENT);
          send_to_char(WELC_MESSG, d->character);
          d->character->next = character_list;
          character_list = d->character;

          char_to_room(d->character, 18221);
          d->character->player.hometown = 18221;

          d->character->specials.tick = plr_tick_count++;
          if (plr_tick_count == PLR_TICK_WRAP)
            plr_tick_count = 0;

          act("$n has entered the game.", TRUE, d->character, 0, 0, TO_ROOM);
          STATE(d) = CON_PLYNG;
          if (!get_max_level(d->character))
            do_start(d->character);
          do_look(d->character, "", "look");
          d->prompt_mode = 1;
          break;

        }
        else {
          SEND_TO_Q("That was an illegal choice.\n\r", d);
          STATE(d) = CON_SLCT;
          break;
        }
      case '4':
        if (get_max_level(d->character) > 5) {

          reset_char(d->character);
          SPRINTF(buf, "Loading %s's equipment", d->character->player.name);
          log_msg(buf);
          load_char_objs(d->character);
          save_char(d->character, AUTO_RENT);
          send_to_char(WELC_MESSG, d->character);
          d->character->next = character_list;
          character_list = d->character;

          char_to_room(d->character, 3606);
          d->character->player.hometown = 3606;

          d->character->specials.tick = plr_tick_count++;
          if (plr_tick_count == PLR_TICK_WRAP)
            plr_tick_count = 0;

          act("$n has entered the game.", TRUE, d->character, 0, 0, TO_ROOM);
          STATE(d) = CON_PLYNG;
          if (!get_max_level(d->character))
            do_start(d->character);
          do_look(d->character, "", "look");
          d->prompt_mode = 1;
          break;

        }
        else {
          SEND_TO_Q("That was an illegal choice.\n\r", d);
          STATE(d) = CON_SLCT;
          break;
        }
      case '5':
        if (get_max_level(d->character) > 5) {

          reset_char(d->character);
          SPRINTF(buf, "Loading %s's equipment", d->character->player.name);
          log_msg(buf);
          load_char_objs(d->character);
          save_char(d->character, AUTO_RENT);
          send_to_char(WELC_MESSG, d->character);
          d->character->next = character_list;
          character_list = d->character;

          char_to_room(d->character, 16107);
          d->character->player.hometown = 16107;

          d->character->specials.tick = plr_tick_count++;
          if (plr_tick_count == PLR_TICK_WRAP)
            plr_tick_count = 0;

          act("$n has entered the game.", TRUE, d->character, 0, 0, TO_ROOM);
          STATE(d) = CON_PLYNG;
          if (!get_max_level(d->character))
            do_start(d->character);
          do_look(d->character, "", "look");
          d->prompt_mode = 1;
          break;

        }
        else {
          SEND_TO_Q("That was an illegal choice.\n\r", d);
          STATE(d) = CON_SLCT;
          break;
        }
      default:
        SEND_TO_Q("That was an illegal choice.\n\r", d);
        STATE(d) = CON_SLCT;
        break;
      }
    }
    break;

  case CON_SLCT:               /* get selection from main menu */
    /* skip whitespaces */
    for (; isspace(*arg); arg++);
    switch (*arg) {
    case '0':
      close_socket(d);
      break;

    case '1':
      reset_char(d->character);
      SPRINTF(buf, "Loading %s's equipment", d->character->player.name);
      log_msg(buf);
      load_char_objs(d->character);
      save_char(d->character, AUTO_RENT);
      send_to_char(WELC_MESSG, d->character);
      d->character->next = character_list;
      character_list = d->character;
      if (d->character->in_room == NOWHERE ||
          d->character->in_room == AUTO_RENT) {
        if (get_max_level(d->character) < LOW_IMMORTAL) {

          if (d->character->specials.start_room <= 0) {
            if (GET_RACE(d->character) == RACE_HALFLING) {
              char_to_room(d->character, 1103);
              d->character->player.hometown = 1103;
            }
            else {
              char_to_room(d->character, 3001);
              d->character->player.hometown = 3001;
            }
          }
          else {
            char_to_room(d->character, d->character->specials.start_room);
            d->character->player.hometown = d->character->specials.start_room;
          }
        }
        else {
          if (d->character->specials.start_room <= NOWHERE) {
            char_to_room(d->character, 1000);
            d->character->player.hometown = 1000;
          }
          else {
            if (real_roomp(d->character->specials.start_room)) {
              char_to_room(d->character, d->character->specials.start_room);
              d->character->player.hometown =
                d->character->specials.start_room;
            }
            else {
              char_to_room(d->character, 1000);
              d->character->player.hometown = 1000;
            }
          }
        }
      }
      else {
        if (real_roomp(d->character->in_room)) {
          char_to_room(d->character, d->character->in_room);
          d->character->player.hometown = d->character->in_room;
        }
        else {
          char_to_room(d->character, 3001);
          d->character->player.hometown = 3001;
        }
      }

      d->character->specials.tick = plr_tick_count++;
      if (plr_tick_count == PLR_TICK_WRAP)
        plr_tick_count = 0;

      act("$n has entered the game.", TRUE, d->character, 0, 0, TO_ROOM);
      STATE(d) = CON_PLYNG;
      if (!get_max_level(d->character))
        do_start(d->character);
      do_look(d->character, "", "look");
      d->prompt_mode = 1;
      break;

    case '2':
      SEND_TO_Q
        ("Enter a text you'd like others to see when they look at you.\n\r",
         d);
      SEND_TO_Q("Terminate with a '@'.\n\r", d);
      if (d->character->player.description) {
        SEND_TO_Q("Old description :\n\r", d);
        SEND_TO_Q(d->character->player.description, d);
        free(d->character->player.description);
        d->character->player.description = 0;
      }
      d->str = &d->character->player.description;
      d->max_str = 240;
      STATE(d) = CON_EXDSCR;
      break;

    case '3':
      SEND_TO_Q(STORY, d);
      STATE(d) = CON_RMOTD;
      break;
    case '4':
      SEND_TO_Q("Enter a new password: ", d);

      write(d->descriptor, echo_off, 4);

      STATE(d) = CON_PWDNEW;
      break;

    case '5':
      SEND_TO_Q("Where would you like to enter?\n\r", d);
      SEND_TO_Q("1.    Midgaard\n\r", d);
      SEND_TO_Q("2.    Shire\n\r", d);
      if (get_max_level(d->character) > 5)
        SEND_TO_Q("3.    Mordilnia\n\r", d);
      if (get_max_level(d->character) > 10)
        SEND_TO_Q("4.    New  Thalos\n\r", d);
      if (get_max_level(d->character) > 20)
        SEND_TO_Q("5.    The Gypsy Village\n\r", d);
      SEND_TO_Q("Your choice? ", d);
      STATE(d) = CON_CITY_CHOICE;
      break;

    case 'D':{
        int i;
        struct char_file_u ch_st;
        FILE *char_file;

        for (i = 0; i <= top_of_p_table; i++) {
          if (!str_cmp((player_table + i)->name, GET_NAME(d->character))) {
            free((player_table + i)->name);
            (player_table + i)->name = (char *)malloc(strlen("111111"));
            strcpy((player_table + i)->name, "111111");
            break;
          }
        }
        /* get the structure from player_table[i].nr */
        if (!(char_file = fopen(PLAYER_FILE, "r+"))) {
          perror("Opening player file for updating. (interpreter.c, nanny)");
          assert(0);
        }
        fseek(char_file, (long)(player_table[i].nr *
                                sizeof(struct char_file_u)), 0);

        /* read in the char, change the name, write back */
        fread(&ch_st, sizeof(struct char_file_u), 1, char_file);
        SPRINTF(ch_st.name, "111111");
        fseek(char_file, (long)(player_table[i].nr *
                                sizeof(struct char_file_u)), 0);
        fwrite(&ch_st, sizeof(struct char_file_u), 1, char_file);
        fclose(char_file);

        close_socket(d);
        break;
      }

    default:
      SEND_TO_Q("Wrong option.\n\r", d);
      SEND_TO_Q(MENU, d);
      break;
    }
    break;

  case CON_PWDNEW:
    /* skip whitespaces */
    for (; isspace(*arg); arg++);

    if (!*arg || strlen(arg) > 10) {
      write(d->descriptor, echo_on, 6);

      SEND_TO_Q("Illegal password.\n\r", d);
      SEND_TO_Q("Password: ", d);

      write(d->descriptor, echo_off, 4);


      return;
    }

    strncpy(d->pwd, crypt(arg, d->character->player.name), 10);
    *(d->pwd + 10) = '\0';
    write(d->descriptor, echo_on, 6);

    SEND_TO_Q("Please retype password: ", d);

    STATE(d) = CON_PWDNCNF;
    write(d->descriptor, echo_off, 4);


    break;
  case CON_PWDNCNF:
    /* skip whitespaces */
    for (; isspace(*arg); arg++);

    if (strncmp(crypt(arg, d->character->player.name), d->pwd, 10)) {
      write(d->descriptor, echo_on, 6);
      SEND_TO_Q("Passwords don't match.\n\r", d);
      SEND_TO_Q("Retype password: ", d);
      write(d->descriptor, echo_off, 4);

      STATE(d) = CON_PWDNEW;
      return;
    }
    write(d->descriptor, echo_on, 6);

    SEND_TO_Q("\n\rDone. You must enter the game to make the change final\n\r",
              d);
    SEND_TO_Q(MENU, d);
    STATE(d) = CON_SLCT;
    break;
  default:
    log_msg("Nanny: illegal state of con'ness");
    abort();
    break;
  }
}

char *class_selections[] = {
  "(M)age",
  "(C)leric",
  "(W)arrior",
  "(T)hief",
  "(D)ruid",
  "mon(K)",
  "\n"
};

int check_valid_class(struct descriptor_data *d, int class) {
  extern int RacialMax[][6];

  if (GET_RACE(d->character) == RACE_HUMANTWO)
    return (TRUE);

  if (class == CLASS_MONK)
    return (FALSE);

  if (class == CLASS_DRUID) {
    if (GET_RACE(d->character) == RACE_VEGMAN)  /* NOT VEGGIE! */
      return (TRUE);
    else
      return (FALSE);
  }

  if (class == CLASS_MAGIC_USER && RacialMax[GET_RACE(d->character)][0] > 10)
    return (TRUE);

  if (class == CLASS_CLERIC && RacialMax[GET_RACE(d->character)][1] > 10)
    return (TRUE);

  if (class == CLASS_WARRIOR && RacialMax[GET_RACE(d->character)][2] > 10)
    return (TRUE);

  if (class == CLASS_THIEF && RacialMax[GET_RACE(d->character)][3] > 10)
    return (TRUE);

  return (FALSE);
}

void display_race_classes(struct descriptor_data *d) {
  extern int RacialMax[][6];
  int i;
  bool bart = FALSE;
  char buf[40];

  int classes[MAX_CLASS];
  classes[0] = CLASS_MAGIC_USER;
  classes[1] = CLASS_CLERIC;
  classes[2] = CLASS_WARRIOR;
  classes[3] = CLASS_THIEF;
  classes[4] = CLASS_DRUID;
  classes[5] = CLASS_MONK;

  if (GET_RACE(d->character) == RACE_HUMAN) {
    SEND_TO_Q("Humans can only be single class characters.  They can choose",
              d);
    SEND_TO_Q("\n\rfrom any class though.", d);
  }

  SEND_TO_Q("\n\rYou can choose from the following classes:\n\r", d);
  SEND_TO_Q
    ("The number in brackets is the maximum level your race can attain.\n\r",
     d);
  for (i = bart = 0; i < MAX_CLASS; i++) {
    if (check_valid_class(d, classes[i])) {
      if (bart)
        SEND_TO_Q(", ", d);
      SPRINTF(buf, "%s [%d]", class_selections[i],
              RacialMax[GET_RACE(d->character)][i]);
      SEND_TO_Q(buf, d);
      bart = TRUE;
    }
  }
  SEND_TO_Q("\n\r", d);
  if (GET_RACE(d->character) != RACE_HUMAN)
    SEND_TO_Q("Enter M/W or M/C/T, T/M (etc) to be multi-classed.\n\r", d);
  SEND_TO_Q("Enter ? for help.\n\rYour choice: ", d);
}

void display_races(struct descriptor_data *d) {
  int i;
  char buf[80];

  SEND_TO_Q("     ", d);
  for (i = 1; RaceList[i - 1].what[0] != '\n'; i++) {
    SPRINTF(buf, "%2d - %-15s ", i, RaceList[i - 1].what);
    if (!(i % 3))
      strcat(buf, "\n\r     ");
    SEND_TO_Q(buf, d);
  }
  SEND_TO_Q("\n\rAn (*) signifies that this race has infravision.\n\r", d);
  SEND_TO_Q("\n\rFor more help on a race, enter ?#, where # is the number", d);
  SEND_TO_Q("\n\rof the race you want more information on.\n\rYour choice? ",
            d);
}


int get_racial_alignment(struct descriptor_data *d) {
  switch (GET_RACE(d->character)) {

    /* Good Aligned */
  case RACE_SMURF:
  case RACE_FAERIE:
    return (500);
    break;

  case RACE_DEMON:
  case RACE_DEVIL:
    return (-1000);
    break;
  case RACE_UNDEAD:
  case RACE_LYCANTH:
  case RACE_ORC:
  case RACE_GOBLIN:
  case RACE_TROLL:
  case RACE_DROW:
  case RACE_VAMPIRE:
  case RACE_OGRE:
  case RACE_SARTAN:
  case RACE_ENFAN:
  case RACE_MFLAYER:
  case RACE_ROO:
    return (-500);
    break;
  default:
    return (0);
  }
}
