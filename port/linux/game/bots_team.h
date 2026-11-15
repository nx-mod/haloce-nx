/* bots_team.h: what a team of bots of either kind agrees on (port/linux/game/bots_team.c)

Both kinds of bots (bots_common.h) play capture the flag's carrier by one
set of rules, kept here once a tick for each team:

- the team's carrier of the other team's flag: the player holding it, or,
  the flag lying away from its stand, the one who let it go to fight (still
  the carrier while alive and near it), or the bot of the team nearest it
  (the carrier killed, or the flag dropped by a player gone);
- where the carrier goes: home to score, or, the gametype's captures needing
  the team's own flag at home and it away, to a hiding place near home (not
  the stand, which the enemy watches) until the flag is back; back to the
  flag when it let it go;
- whether it lets the flag go to fight: hiding, an enemy in sight and near
  (a flag holder cannot fire; a flag lying 17 s untouched goes home:
  bots_team_ctf_regrab_now says when to go back for it);
- the team's other bots leave the other team's flag to the carrier when it
  lies dropped, and one bot (the nearest) guards the hiding carrier.

And a vehicle's crew (bots_team_crew_*): a driver in a Warthog, bot or
person, calls for the free bots of its team nearest it, one or two, the
gunner's seat first, who drop what they were doing and board it; a bot
driver waits for them a few seconds. */
#ifndef HALO_BOTS_TEAM_H
#define HALO_BOTS_TEAM_H

enum
{
	_bots_carry_none = 0,
	/* holding the flag: to the team's stand, to score */
	_bots_carry_home,
	/* holding the flag, the team's own away (captures need it at home): hide */
	_bots_carry_hide,
	/* the carrier, the flag lying: back to it */
	_bots_carry_regrab,
};

/* once a tick, before the bots' (bots_update_actions) */
void bots_team_update(void);
/* a new map: nothing kept */
void bots_team_reset(void);

/* capture the flag: the team's carrier of the other team's flag (NONE) */
long bots_team_ctf_carrier(long team_index);
/* what player `player_index` does as its team's carrier and where (the
stand, the hiding place, the flag); _bots_carry_none if it is not the carrier */
short bots_team_ctf_carry(long player_index, real_point3d *point);
/* whether the carrier, holding the flag, lets it go now to fight the enemy
it sees that far away (hiding, settled) */
boolean bots_team_ctf_drop(long player_index, boolean enemy_visible, real enemy_distance);
/* whether the carrier, the flag lying, goes back for it now whatever it
fights (its return near, or nothing to fight for a moment) */
boolean bots_team_ctf_regrab_now(long player_index, boolean fighting);
/* whether player `player_index` leaves the other team's flag alone (lying,
its team's carrier another) */
boolean bots_team_ctf_flag_left(long player_index);
/* whether player `player_index` guards its team's hiding carrier, and where */
boolean bots_team_ctf_escort(long player_index, real_point3d *point);

/* what each kind tells of its bots each tick, for the crews' calls: a bot
fighting (an enemy close and seen), or one not to be called (the flag's
carrier, a defender, a bot on its way to a powerup) */
enum
{
	_bots_team_state_fighting = 1 << 0,
	_bots_team_state_exempt = 1 << 1,
};
void bots_team_bot_state(long player_index, unsigned long state);

/* crews: the Warthog whose crew bot player `player_index` is called to and
the seat (the gunner's, then a passenger's), or NONE; a passenger's seat is
the bot's to refuse (bots_team_crew_refuse: not its way) */
long bots_team_crew_call(long player_index, short *seat_index);
void bots_team_crew_refuse(long player_index);
/* whether a bot driving vehicle `vehicle_index` waits for its crew still
(called and on its way, 6 s at most from the call) */
boolean bots_team_crew_wait(long vehicle_index);

#endif
