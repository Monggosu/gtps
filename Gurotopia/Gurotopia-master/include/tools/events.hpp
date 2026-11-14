#pragma once

/* server-wide boosts started by an administrator (/boostgem, /boostxp). not saved: a restart ends them. */

/* @return the multiplier for gems picked up / xp earned right now (1.0 when no boost is running) */
extern double gem_multiplier();
extern double xp_multiplier();

/* @param minutes 0 stops the boost */
extern void set_boost(bool xp, double multiplier, int minutes);
extern long long boost_seconds_left(bool xp); // @note 0 if none
