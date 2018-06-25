/*$file${.::tunnel.c} ######################################################*/
/*
* Model: game.qm
* File:  ${.::tunnel.c}
*
* This code has been generated by QM 4.3.0 (https://www.state-machine.com/qm).
* DO NOT EDIT THIS FILE MANUALLY. All your changes will be lost.
*
* This program is open source software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as published
* by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
*/
/*$endhead${.::tunnel.c} ###################################################*/
#include "qpc.h"
#include "bsp.h"
#include "game.h"

Q_DEFINE_THIS_FILE

/* local objects -----------------------------------------------------------*/
/*$declare${AOs::Tunnel} ###################################################*/
/*${AOs::Tunnel} ...........................................................*/
typedef struct {
/* protected: */
    QActive super;

/* private: */
    QTimeEvt blinkTimeEvt;
    QTimeEvt screenTimeEvt;
    QMsm * mines[GAME_MINES_MAX];
    QMsm * mine1_pool[GAME_MINES_MAX];
    QMsm * mine2_pool[GAME_MINES_MAX];
    uint8_t blink_ctr;
    uint8_t last_mine_x;
    uint8_t last_mine_y;
    uint8_t wall_thickness_top;
    uint8_t wall_thickness_bottom;
    uint8_t wall_gap;
} Tunnel;

/* private: */
static void Tunnel_advance(Tunnel * const me);
static void Tunnel_plantMine(Tunnel * const me);
static void Tunnel_dispatchToAllMines(Tunnel * const me, QEvt const * e);

/* protected: */
static QState Tunnel_initial(Tunnel * const me, QEvt const * const e);
static QState Tunnel_active(Tunnel * const me, QEvt const * const e);
static QState Tunnel_show_logo(Tunnel * const me, QEvt const * const e);
static QState Tunnel_demo(Tunnel * const me, QEvt const * const e);
static QState Tunnel_playing(Tunnel * const me, QEvt const * const e);
static QState Tunnel_game_over(Tunnel * const me, QEvt const * const e);
static QState Tunnel_screen_saver(Tunnel * const me, QEvt const * const e);
static QState Tunnel_screen_saver_hide(Tunnel * const me, QEvt const * const e);
static QState Tunnel_screen_saver_show(Tunnel * const me, QEvt const * const e);
static QState Tunnel_final(Tunnel * const me, QEvt const * const e);
/*$enddecl${AOs::Tunnel} ###################################################*/
static Tunnel l_tunnel; /* the sole instance of the Tunnel active object */

/* Public-scope objects ----------------------------------------------------*/
/* Check for the minimum required QP version */
#if (QP_VERSION < 630U) || (QP_VERSION != ((QP_RELEASE^4294967295U) % 0x3E8U))
#error qpc version 6.3.0 or higher required
#endif

/*$define${AOs::AO_Tunnel} #################################################*/

/* opaque AO pointer */
/*${AOs::AO_Tunnel} ........................................................*/
QActive * const AO_Tunnel = &l_tunnel.super;
/*$enddef${AOs::AO_Tunnel} #################################################*/

/* Active object definition ================================================*/
/*$define${AOs::Tunnel_ctor} ###############################################*/
/*${AOs::Tunnel_ctor} ......................................................*/
void Tunnel_ctor(void) {
    uint8_t n;
    Tunnel *me = &l_tunnel;
    QActive_ctor(&me->super, Q_STATE_CAST(&Tunnel_initial));
    QTimeEvt_ctorX(&me->blinkTimeEvt,  &me->super, BLINK_TIMEOUT_SIG,  0U);
    QTimeEvt_ctorX(&me->screenTimeEvt, &me->super, SCREEN_TIMEOUT_SIG, 0U);
    for (n = 0; n < GAME_MINES_MAX; ++n) {
        me->mine1_pool[n] = Mine1_ctor(n); /* instantiate Mine1 in the pool */
        me->mine2_pool[n] = Mine2_ctor(n); /* instantiate Mine2 in the pool */
        me->mines[n] = (QMsm *)0; /* mine 'n' is unused */
    }
    me->last_mine_x = 0; /* the last mine at the right edge of the tunnel */
    me->last_mine_y = 0;
}
/*$enddef${AOs::Tunnel_ctor} ###############################################*/
/*$define${AOs::Tunnel} ####################################################*/
/*${AOs::Tunnel} ...........................................................*/
/*${AOs::Tunnel::advance} ..................................................*/
static void Tunnel_advance(Tunnel * const me) {
    uint32_t rnd;

    rnd = (BSP_random() & 0xFFU);

    /* reduce the top wall thickness 18.75% of the time */
    if ((rnd < 48U) && (me->wall_thickness_top > 0U)) {
        --me->wall_thickness_top;
    }

    /* reduce the bottom wall thickness 18.75% of the time */
    if ((rnd > 208U) && (me->wall_thickness_bottom > 0U)) {
        --me->wall_thickness_bottom;
    }

    rnd = (BSP_random() & 0xFFU);

    /* grow the bottom wall thickness 19.14% of the time */
    if ((rnd < 49U)
        && ((GAME_TUNNEL_HEIGHT
             - me->wall_thickness_top
             - me->wall_thickness_bottom) > me->wall_gap))
    {
        ++me->wall_thickness_bottom;
    }

    /* grow the top wall thickness 19.14% of the time */
    if ((rnd > 207U)
        && ((GAME_TUNNEL_HEIGHT
             - me->wall_thickness_top
             - me->wall_thickness_bottom) > me->wall_gap))
    {
        ++me->wall_thickness_top;
    }

    /* advance the Tunnel by 1 game step to the left
    * and copy the Tunnel layer to the main frame buffer
    */
    BSP_advanceWalls(me->wall_thickness_top, me->wall_thickness_bottom);
}

/*${AOs::Tunnel::plantMine} ................................................*/
static void Tunnel_plantMine(Tunnel * const me) {
    uint32_t rnd = (BSP_random() & 0xFFU);

    if (me->last_mine_x > 0U) {
        --me->last_mine_x; /* shift the last Mine 1 position to the left */
    }
    /* last mine far enough? */
    if ((me->last_mine_x + GAME_MINES_DIST_MIN < GAME_TUNNEL_WIDTH)
        && (rnd < 8U)) /* place the mines only 5% of the time */
    {
        uint8_t n;
        for (n = 0U; n < Q_DIM(me->mines); ++n) { /*look for disabled mines */
            if (me->mines[n] == (QMsm *)0) {
                break;
            }
        }
        if (n < Q_DIM(me->mines)) { /* a disabled Mine found? */
            ObjectPosEvt ope; /* event to dispatch to the Mine */
            rnd = (BSP_random() & 0xFFFFU);

            if ((rnd & 1U) == 0U) { /* choose the type of the mine */
                me->mines[n] = me->mine1_pool[n];
            }
            else {
                me->mines[n] = me->mine2_pool[n];
            }

            /* new Mine is planted by the end of the tunnel */
            me->last_mine_x = GAME_TUNNEL_WIDTH - 8U;

            /* choose a random y-position for the Mine in the Tunnel */
            rnd %= (GAME_TUNNEL_HEIGHT
                    - me->wall_thickness_top
                    - me->wall_thickness_bottom - 4U);
            me->last_mine_y = (uint8_t)(me->wall_thickness_top + 2U + rnd);

            ope.super.sig = MINE_PLANT_SIG;
            ope.x = me->last_mine_x;
            ope.y = me->last_mine_y;
            QHSM_DISPATCH(me->mines[n], (QEvt *)&ope); /* direct dispatch */
        }
    }
}

/*${AOs::Tunnel::dispatchToAllMines} .......................................*/
static void Tunnel_dispatchToAllMines(Tunnel * const me, QEvt const * e) {
    uint8_t n;

    for (n = 0U; n < GAME_MINES_MAX; ++n) {
        if (me->mines[n] != (QMsm *)0) { /* is the mine used? */
            QHSM_DISPATCH(me->mines[n], e);
        }
    }
}

/*${AOs::Tunnel::SM} .......................................................*/
static QState Tunnel_initial(Tunnel * const me, QEvt const * const e) {
    /*${AOs::Tunnel::SM::initial} */
    uint8_t n;
    for (n = 0U; n < GAME_MINES_MAX; ++n) {
        QHSM_INIT(me->mine1_pool[n], (QEvt *)0);/*initial tran. for Mine1 */
        QHSM_INIT(me->mine2_pool[n], (QEvt *)0);/*initial tran. for Mine2 */
    }
    BSP_randomSeed(1234); /* seed the pseudo-random generator */

    QActive_subscribe(&me->super, TIME_TICK_SIG);
    QActive_subscribe(&me->super, PLAYER_TRIGGER_SIG);
    QActive_subscribe(&me->super, PLAYER_QUIT_SIG);

    /* object dictionaries... */
    QS_OBJ_DICTIONARY(&l_tunnel.blinkTimeEvt);
    QS_OBJ_DICTIONARY(&l_tunnel.screenTimeEvt);

    /* local signals... */
    QS_SIG_DICTIONARY(BLINK_TIMEOUT_SIG,  me);
    QS_SIG_DICTIONARY(SCREEN_TIMEOUT_SIG, me);
    QS_SIG_DICTIONARY(SHIP_IMG_SIG,       me);
    QS_SIG_DICTIONARY(MISSILE_IMG_SIG,    me);
    QS_SIG_DICTIONARY(MINE_IMG_SIG,       me);
    QS_SIG_DICTIONARY(MINE_DISABLED_SIG,  me);
    QS_SIG_DICTIONARY(EXPLOSION_SIG,      me);
    QS_SIG_DICTIONARY(SCORE_SIG,          me);

    (void)e;  /* unused parameter */

    QS_FUN_DICTIONARY(&Tunnel_active);
    QS_FUN_DICTIONARY(&Tunnel_show_logo);
    QS_FUN_DICTIONARY(&Tunnel_demo);
    QS_FUN_DICTIONARY(&Tunnel_playing);
    QS_FUN_DICTIONARY(&Tunnel_game_over);
    QS_FUN_DICTIONARY(&Tunnel_screen_saver);
    QS_FUN_DICTIONARY(&Tunnel_screen_saver_hide);
    QS_FUN_DICTIONARY(&Tunnel_screen_saver_show);
    QS_FUN_DICTIONARY(&Tunnel_final);

    return Q_TRAN(&Tunnel_show_logo);
}
/*${AOs::Tunnel::SM::active} ...............................................*/
static QState Tunnel_active(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::active::MINE_DISABLED} */
        case MINE_DISABLED_SIG: {
            Q_ASSERT((Q_EVT_CAST(MineEvt)->id < GAME_MINES_MAX)
                     && (me->mines[Q_EVT_CAST(MineEvt)->id] != (QMsm *)0));
            me->mines[Q_EVT_CAST(MineEvt)->id] = (QMsm *)0;
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::PLAYER_QUIT} */
        case PLAYER_QUIT_SIG: {
            status_ = Q_TRAN(&Tunnel_final);
            break;
        }
        default: {
            status_ = Q_SUPER(&QHsm_top);
            break;
        }
    }
    return status_;
}
/*${AOs::Tunnel::SM::active::show_logo} ....................................*/
static QState Tunnel_show_logo(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::active::show_logo} */
        case Q_ENTRY_SIG: {
            QTimeEvt_armX(&me->blinkTimeEvt, BSP_TICKS_PER_SEC/2U,
                          BSP_TICKS_PER_SEC/2U);
            QTimeEvt_armX(&me->screenTimeEvt, BSP_TICKS_PER_SEC*5U, 0U);
            me->blink_ctr = 0U;
            BSP_paintString(24U, (GAME_TUNNEL_HEIGHT / 2U) - 8U, "Quantum LeAps");
            BSP_paintString(16U, (GAME_TUNNEL_HEIGHT / 2U) + 0U, "state-machine.com");

            BSP_paintString(1U, GAME_TUNNEL_HEIGHT - 18U, "Fire missile: BTN0");
            BSP_paintString(1U, GAME_TUNNEL_HEIGHT - 10U, "Fly ship up:  BTN1");

            BSP_updateScreen();
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::show_logo} */
        case Q_EXIT_SIG: {
            QTimeEvt_disarm(&me->blinkTimeEvt);
            QTimeEvt_disarm(&me->screenTimeEvt);
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::show_logo::SCREEN_TIMEOUT} */
        case SCREEN_TIMEOUT_SIG: {
            status_ = Q_TRAN(&Tunnel_demo);
            break;
        }
        /*${AOs::Tunnel::SM::active::show_logo::BLINK_TIMEOUT} */
        case BLINK_TIMEOUT_SIG: {
            me->blink_ctr ^= 1U; /* toggle the blink counter */
            /*${AOs::Tunnel::SM::active::show_logo::BLINK_TIMEOUT::[me->blink_ctr==0U]} */
            if (me->blink_ctr == 0U) {
                BSP_paintString(24U + 8U*6U, (GAME_TUNNEL_HEIGHT / 2U) - 8U, "LeAps");
                BSP_updateScreen();
                status_ = Q_HANDLED();
            }
            /*${AOs::Tunnel::SM::active::show_logo::BLINK_TIMEOUT::[else]} */
            else {
                BSP_paintString(24U + 8U*6U, (GAME_TUNNEL_HEIGHT / 2U) - 8U, "LeaPs");
                BSP_updateScreen();
                status_ = Q_HANDLED();
            }
            break;
        }
        default: {
            status_ = Q_SUPER(&Tunnel_active);
            break;
        }
    }
    return status_;
}
/*${AOs::Tunnel::SM::active::demo} .........................................*/
static QState Tunnel_demo(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::active::demo} */
        case Q_ENTRY_SIG: {
            me->last_mine_x = 0U; /* last mine at right edge of the tunnel */
            me->last_mine_y = 0U;
            /* set the tunnel properties... */
            me->wall_thickness_top = 0U;
            me->wall_thickness_bottom = 0U;
            me->wall_gap = GAME_WALLS_GAP_Y;

            /* clear the tunnel walls */
            BSP_clearWalls();

            QTimeEvt_armX(&me->blinkTimeEvt,  BSP_TICKS_PER_SEC/2U,
                          BSP_TICKS_PER_SEC/2U); /* every 1/2 sec */
            QTimeEvt_armX(&me->screenTimeEvt, BSP_TICKS_PER_SEC*20U, 0U);

            me->blink_ctr = 0U; /* init the blink counter */
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::demo} */
        case Q_EXIT_SIG: {
            QTimeEvt_disarm(&me->blinkTimeEvt);
            QTimeEvt_disarm(&me->screenTimeEvt);
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::demo::BLINK_TIMEOUT} */
        case BLINK_TIMEOUT_SIG: {
            me->blink_ctr ^= 1U; /* toggle the blink cunter */
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::demo::SCREEN_TIMEOUT} */
        case SCREEN_TIMEOUT_SIG: {
            status_ = Q_TRAN(&Tunnel_screen_saver);
            break;
        }
        /*${AOs::Tunnel::SM::active::demo::TIME_TICK} */
        case TIME_TICK_SIG: {
            Tunnel_advance(me);
            if (me->blink_ctr != 0U) {
                /* add the text bitmap into the frame buffer */
                BSP_paintString((GAME_TUNNEL_WIDTH - 10U*6U)/2U,
                                (GAME_TUNNEL_HEIGHT - 4U)/2U,
                                "Press BTN0");
            }
            BSP_updateScreen();
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::demo::PLAYER_TRIGGER} */
        case PLAYER_TRIGGER_SIG: {
            status_ = Q_TRAN(&Tunnel_playing);
            break;
        }
        default: {
            status_ = Q_SUPER(&Tunnel_active);
            break;
        }
    }
    return status_;
}
/*${AOs::Tunnel::SM::active::playing} ......................................*/
static QState Tunnel_playing(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::active::playing} */
        case Q_ENTRY_SIG: {
            static QEvt const takeoff = { TAKE_OFF_SIG, 0U, 0U };
            me->wall_gap = GAME_WALLS_GAP_Y;
            QACTIVE_POST(AO_Ship, &takeoff, me); /* post the TAKEOFF sig */
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::playing} */
        case Q_EXIT_SIG: {
            QEvt recycle;
            recycle.sig = MINE_RECYCLE_SIG;
            Tunnel_dispatchToAllMines(me, &recycle); /* recycle all Mines */
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::playing::TIME_TICK} */
        case TIME_TICK_SIG: {
            /* render this frame on the display */
            BSP_updateScreen();
            Tunnel_advance(me);
            Tunnel_plantMine(me);
            Tunnel_dispatchToAllMines(me, e);
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::playing::SHIP_IMG} */
        case SHIP_IMG_SIG: {
            uint8_t x   = Q_EVT_CAST(ObjectImageEvt)->x;
            int8_t  y   = Q_EVT_CAST(ObjectImageEvt)->y;
            uint8_t bmp = Q_EVT_CAST(ObjectImageEvt)->bmp;

            /* did the Ship/Missile hit the tunnel wall? */
            if (BSP_isWallHit(bmp, x, y)) {
                static QEvt const hit = { HIT_WALL_SIG, 0U, 0U };
                QACTIVE_POST(AO_Ship, &hit, me);
            }
            BSP_paintBitmap(x, y, bmp);
            Tunnel_dispatchToAllMines(me, e); /* let Mines check for hits */
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::playing::MISSILE_IMG} */
        case MISSILE_IMG_SIG: {
            uint8_t x   = Q_EVT_CAST(ObjectImageEvt)->x;
            int8_t  y   = Q_EVT_CAST(ObjectImageEvt)->y;
            uint8_t bmp = Q_EVT_CAST(ObjectImageEvt)->bmp;

            /* did the Ship/Missile hit the tunnel wall? */
            if (BSP_isWallHit(bmp, x, y)) {
                static QEvt const hit = { HIT_WALL_SIG, 0U, 0U };
                QACTIVE_POST(AO_Missile, &hit, me);
            }
            BSP_paintBitmap(x, y, bmp);
            Tunnel_dispatchToAllMines(me, e); /* let Mines check for hits */
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::playing::MINE_IMG} */
        case MINE_IMG_SIG: {
            BSP_paintBitmap(Q_EVT_CAST(ObjectImageEvt)->x,
                            Q_EVT_CAST(ObjectImageEvt)->y,
                            Q_EVT_CAST(ObjectImageEvt)->bmp);
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::playing::EXPLOSION} */
        case EXPLOSION_SIG: {
            BSP_paintBitmap(Q_EVT_CAST(ObjectImageEvt)->x,
                            Q_EVT_CAST(ObjectImageEvt)->y,
                            Q_EVT_CAST(ObjectImageEvt)->bmp);
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::playing::SCORE} */
        case SCORE_SIG: {
            BSP_updateScore(Q_EVT_CAST(ScoreEvt)->score);
            /* increase difficulty of the game:
            * the tunnel gets narrower as the score goes up
            */
            me->wall_gap = (uint8_t)(GAME_WALLS_GAP_Y
                              - Q_EVT_CAST(ScoreEvt)->score/100U);
            if (me->wall_gap < GAME_WALLS_MIN_GAP_Y) {
                me->wall_gap = GAME_WALLS_MIN_GAP_Y;
            }
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::playing::GAME_OVER} */
        case GAME_OVER_SIG: {
            BSP_clearWalls();
            BSP_updateScore(Q_EVT_CAST(ScoreEvt)->score);
            BSP_updateScreen();
            status_ = Q_TRAN(&Tunnel_game_over);
            break;
        }
        default: {
            status_ = Q_SUPER(&Tunnel_active);
            break;
        }
    }
    return status_;
}
/*${AOs::Tunnel::SM::active::game_over} ....................................*/
static QState Tunnel_game_over(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::active::game_over} */
        case Q_ENTRY_SIG: {
            QTimeEvt_armX(&me->blinkTimeEvt, BSP_TICKS_PER_SEC/2U,
                          BSP_TICKS_PER_SEC/2U); /* every 1/2 sec */
            QTimeEvt_armX(&me->screenTimeEvt, BSP_TICKS_PER_SEC*5U, 0U);
            me->blink_ctr = 0U;
            BSP_paintString((GAME_TUNNEL_WIDTH - 6U * 9U) / 2U,
                            (GAME_TUNNEL_HEIGHT / 2U) - 4U,
                            "Game Over");
            BSP_updateScreen();
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::game_over} */
        case Q_EXIT_SIG: {
            QTimeEvt_disarm(&me->blinkTimeEvt);
            QTimeEvt_disarm(&me->screenTimeEvt);
            BSP_updateScore(0); /* update the score on the display */
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::game_over::BLINK_TIMEOUT} */
        case BLINK_TIMEOUT_SIG: {
            me->blink_ctr ^= 1U; /* toggle the blink counter */
            BSP_paintString((GAME_TUNNEL_WIDTH - 6U*9U) / 2U,
                            (GAME_TUNNEL_HEIGHT / 2U) - 4U,
                            ((me->blink_ctr == 0U)
                            ? "Game Over"
                            : "         "));
            BSP_updateScreen();
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::game_over::SCREEN_TIMEOUT} */
        case SCREEN_TIMEOUT_SIG: {
            status_ = Q_TRAN(&Tunnel_demo);
            break;
        }
        default: {
            status_ = Q_SUPER(&Tunnel_active);
            break;
        }
    }
    return status_;
}
/*${AOs::Tunnel::SM::active::screen_saver} .................................*/
static QState Tunnel_screen_saver(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::active::screen_saver::initial} */
        case Q_INIT_SIG: {
            status_ = Q_TRAN(&Tunnel_screen_saver_hide);
            break;
        }
        /*${AOs::Tunnel::SM::active::screen_saver::PLAYER_TRIGGER} */
        case PLAYER_TRIGGER_SIG: {
            status_ = Q_TRAN(&Tunnel_demo);
            break;
        }
        default: {
            status_ = Q_SUPER(&Tunnel_active);
            break;
        }
    }
    return status_;
}
/*${AOs::Tunnel::SM::active::screen_saver::screen_saver_hide} ..............*/
static QState Tunnel_screen_saver_hide(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::active::screen_saver::screen_saver_hide} */
        case Q_ENTRY_SIG: {
            BSP_displayOff(); /* power down the display */
            QTimeEvt_armX(&me->screenTimeEvt, BSP_TICKS_PER_SEC*3U, 0U);
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::screen_saver::screen_saver_hide} */
        case Q_EXIT_SIG: {
            QTimeEvt_disarm(&me->screenTimeEvt);
            BSP_displayOn(); /* power up the display */
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::screen_saver::screen_saver_hid~::SCREEN_TIMEOUT} */
        case SCREEN_TIMEOUT_SIG: {
            status_ = Q_TRAN(&Tunnel_screen_saver_show);
            break;
        }
        default: {
            status_ = Q_SUPER(&Tunnel_screen_saver);
            break;
        }
    }
    return status_;
}
/*${AOs::Tunnel::SM::active::screen_saver::screen_saver_show} ..............*/
static QState Tunnel_screen_saver_show(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::active::screen_saver::screen_saver_show} */
        case Q_ENTRY_SIG: {
            uint32_t rnd = BSP_random();
            /* clear the screen frame buffer */
            BSP_clearFB();
            BSP_paintString((uint8_t)(rnd % (GAME_TUNNEL_WIDTH - 10U*6U)),
                            (uint8_t) (rnd % (GAME_TUNNEL_HEIGHT - 8U)),
                            "Press BTN0");
            BSP_updateScreen();
            QTimeEvt_armX(&me->screenTimeEvt, BSP_TICKS_PER_SEC/2U, 0U);
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::screen_saver::screen_saver_show} */
        case Q_EXIT_SIG: {
            QTimeEvt_disarm(&me->screenTimeEvt);
            BSP_clearFB();
            BSP_updateScreen();
            status_ = Q_HANDLED();
            break;
        }
        /*${AOs::Tunnel::SM::active::screen_saver::screen_saver_sho~::SCREEN_TIMEOUT} */
        case SCREEN_TIMEOUT_SIG: {
            status_ = Q_TRAN(&Tunnel_screen_saver_hide);
            break;
        }
        default: {
            status_ = Q_SUPER(&Tunnel_screen_saver);
            break;
        }
    }
    return status_;
}
/*${AOs::Tunnel::SM::final} ................................................*/
static QState Tunnel_final(Tunnel * const me, QEvt const * const e) {
    QState status_;
    switch (e->sig) {
        /*${AOs::Tunnel::SM::final} */
        case Q_ENTRY_SIG: {
            /* clear the screen */
            BSP_clearFB();
            BSP_updateScreen();
            QF_stop(); /* stop QF and cleanup */
            status_ = Q_HANDLED();
            break;
        }
        default: {
            status_ = Q_SUPER(&QHsm_top);
            break;
        }
    }
    return status_;
}
/*$enddef${AOs::Tunnel} ####################################################*/
