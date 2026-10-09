// engine.d.ts — tipos da game engine CelerOS (engine.js + physics.js).
// Vendorizado no app por `celer.js new --game` / `celer.js engine PASTA`.

declare namespace E {
    const version: string;
    /** Recursos da placa/firmware detectados no init(). */
    interface Caps {
        psram: boolean;
        speaker: boolean;
        music: boolean;      // playMusic (API 25)
        png: boolean;        // drawPNG
        sprites: boolean;    // createSprite
        smooth: boolean;     // fillSmoothCircle (API 22)
        round: boolean;      // fillSmoothRoundRect (API 22)
        gradient: boolean;   // fillGradient (API 22)
        arc: boolean;        // fillArc (API 22)
        wide: boolean;       // drawWideLine/mixColor (API 22)
        slots: number;       // limite do pool (spriteSlots, API 29; 4 em firmware velho)
        native: boolean;     // canvas nativo ativo (API 28)
        w: number;
        h: number;
    }
    interface InitOpts {
        fps?: number;        // alvo de frames (default 30; 0 = sem teto)
        native?: boolean;    // canvas nativo (pixels fisicos, sem topbar)
        keepAwake?: boolean; // default true durante o run
        save?: string;       // prefixo das chaves no Storage NVS
        dir?: string;        // nome da pasta do app (base dos assets)
        bases?: string[];    // prefixos alternativos p/ E.spr.load
        particles?: number;  // tamanho do pool de particulas (default 96)
    }

    const caps: Caps;
    const theme: CelerTheme | null;
    let W: number;
    let H: number;
    let dt: number;
    let fps: number;
    let fpsTarget: number;
    const data: Record<string, any>;
    let sceneName: string;

    function init(opts?: InitOpts): Caps;

    // ------------------------------------------------------------- input --
    interface Input {
        x: number;
        y: number;
        down: boolean;
        justDown: boolean;
        justUp: boolean;
        moved: boolean;
        dx: number;
        dy: number;
        holdDx: number;
        holdDy: number;
        tap: { x: number; y: number } | null;
        swipe: { dir: 'left' | 'right' | 'up' | 'down'; dx: number; dy: number; dist: number } | null;
        longpress: boolean;
        btn: number;         // 0 nada, 1 curto, 2 longo (devkit)
        dragThresh: number;
        tapMs: number;
        swipeMin: number;
        longMs: number;
    }
    const input: Input;
    interface Rect { x: number; y: number; w: number; h: number }
    function hit(b: Rect): boolean;
    function press(b: Rect): boolean;

    // ----------------------------------------------------- cenas e loop --
    interface Scene {
        fps?: number;        // sobrepoe o alvo global nesta cena (0 = sem teto)
        enter?: () => void;
        update?: (dt: number) => void;
        draw?: () => void;
        exit?: () => void;
    }
    function run(scenes: Record<string, Scene>, first: string): void;
    function goto(name: string): void;
    function quit(): void;

    // -------------------------------------------------- timers e tweens --
    interface Timer { dead: boolean }
    interface Tween { dead: boolean }
    function after(ms: number, fn: () => void): Timer;
    function every(ms: number, fn: () => void): Timer;
    function cancel(t: Timer | Tween): void;
    function clearTimers(): void;
    function clearTweens(): void;
    function tween(obj: any, to: Record<string, number>, ms: number,
                   opts?: { ease?: (t: number) => number; delay?: number;
                            onDone?: () => void }): Tween;

    // ------------------------------------------------ grupos e pools -----
    interface Entity {
        dead?: boolean;
        update?: (dt: number) => void;
        draw?: () => void;
        [k: string]: any;
    }
    interface Group {
        items: Entity[];
        count: number;
        add(o: Entity): Entity;
        update(dt: number): void;
        draw(): void;
        each(fn: (o: Entity) => void): void;
        clear(): void;
    }
    function group(): Group;
    interface Pool extends Omit<Group, 'add'> {
        spawn(): Entity | null;
        alive: number;
    }
    function pool(n: number, factory: (i: number) => Entity): Pool;

    // ------------------------------------------------------------ math --
    const m: {
        clamp(v: number, a: number, b: number): number;
        lerp(a: number, b: number, t: number): number;
        map(v: number, a: number, b: number, c: number, d: number): number;
        rand(a?: number, b?: number): number;
        randInt(a: number, b: number): number;
        pick<T>(arr: T[]): T;
        dist(x0: number, y0: number, x1: number, y1: number): number;
        dist2(x0: number, y0: number, x1: number, y1: number): number;
        ang(x0: number, y0: number, x1: number, y1: number): number;
        approach(v: number, target: number, delta: number): number;
        wrap(v: number, a: number, b: number): number;
        sign(v: number): number;
        linear(t: number): number;
        inQuad(t: number): number;
        outQuad(t: number): number;
        inOutQuad(t: number): number;
        outBack(t: number): number;
    };
    function rng(seed: number): () => number;

    // --------------------------------------------------------- sprites --
    interface SpriteDef {
        name: string;
        file?: string;        // <base>/<file>.png (bases do init)
        w: number;
        h: number;
        paint?: (w: number, h: number, x: number, y: number) => void;
    }
    interface SpriteSlot { id: number; w: number; h: number; paint: SpriteDef['paint'] | null }
    const spr: {
        bases: string[];
        load(defs: SpriteDef[], opts?: { bases?: string[] }): Record<string, SpriteSlot>;
        has(name: string): boolean;
        /** true se esta num slot real (blit com cor-chave); false = painter */
        backed(name: string): boolean;
        blit(name: string, x: number, y: number,
             opts?: { key?: number; cx?: boolean; cy?: boolean }): void;
        free(name: string): void;
        freeAll(): void;
    };
    interface Anim {
        i: number;
        done: boolean;
        tick(dt: number): void;
        draw(x: number, y: number, opts?: { key?: number }): void;
    }
    function anim(frames: (string | ((x: number, y: number) => void))[],
                  fps?: number, loop?: boolean): Anim;

    // ---------------------------------------------------------- camera --
    const cam: {
        x: number;
        y: number;
        ox: number;
        oy: number;
        bounds: Rect | null;
        follow(target: { x: number; y: number }, lerp?: number): void;
        shake(pow: number, dur: number): void;
        center(x: number, y: number): void;
        reset(): void;
        wx(x: number): number;
        wy(y: number): number;
    };

    // ------------------------------------------------------------ gfx --
    interface DrawOpts { screen?: boolean; fill?: boolean; r?: number }
    const gfx: {
        rect(x: number, y: number, w: number, h: number, color: number, o?: DrawOpts): void;
        circle(x: number, y: number, r: number, color: number,
               o?: DrawOpts & { smooth?: boolean }): void;
        line(x0: number, y0: number, x1: number, y1: number, color: number,
             o?: DrawOpts & { w?: number }): void;
        tri(x0: number, y0: number, x1: number, y1: number, x2: number, y2: number,
            color: number, o?: DrawOpts): void;
        gradient(x: number, y: number, w: number, h: number, c1: number, c2: number,
                 o?: DrawOpts & { dir?: 'x' | 'y' }): void;
        arc(x: number, y: number, r0: number, r1: number, a0: number, a1: number,
            color: number, o?: DrawOpts): void;
        text(str: any, x: number, y: number, o?: {
            color?: number; bg?: number; size?: number; font?: number;
            align?: 'left' | 'center' | 'right';
            valign?: 'top' | 'middle' | 'bottom'; screen?: boolean;
        }): void;
        button(label: string, x: number, y: number, w: number, h: number, o?: {
            primary?: boolean; color?: number; bg?: number; r?: number; font?: number;
            screen?: boolean;
        }): Rect;
        bar(x: number, y: number, w: number, h: number, frac: number, o?: {
            fg?: number; bg?: number; r?: number; screen?: boolean;
        }): void;
        panel(x: number, y: number, w: number, h: number, o?: {
            bg?: number; stroke?: number; r?: number; screen?: boolean;
        }): void;
    };

    // ------------------------------------------------------------- fx --
    const fx: {
        burst(x: number, y: number, o?: {
            n?: number; color?: number; colors?: number[];
            speed?: number; speed2?: number; angle?: number; spread?: number;
            life?: number; size?: number; grav?: number; drag?: number;
            shape?: 'dot' | 'spark' | 'ring';
        }): void;
        popText(x: number, y: number, str: any, o?: {
            color?: number; life?: number; font?: number; screen?: boolean;
        }): void;
        flash(color: number, ms?: number): void;
        /** onda de choque: anel que expande (speed px/s) e some */
        ring(x: number, y: number, o?: {
            r0?: number; speed?: number; color?: number; life?: number;
        }): void;
        stars(n: number, o?: {
            w?: number; h?: number; vy?: number; color?: number;
            colors?: number[];
        }): {
            update(dt: number): void;
            draw(): void;
        };
        draw(): void;
    };

    // ----------------------------------------------------------- audio --
    const audio: {
        muted: boolean;
        sfxOverMusic: boolean;
        sfxTable: Record<string, number[] | number[][]>;
        music(song: {
            bpm?: number; loops?: number;
            tracks: { wave?: 'sq' | 'sq25' | 'tri' | 'saw'; drum?: boolean;
                      vol?: number; notes: number[][] }[];
        }, opts?: { startMs?: number }): boolean;
        stop(): void;
        playing(): boolean;
        beat(): number;      // -1 sem musica
        sfx(what: string | number[] | number[][]): void;
        /** Abafa a trilha por ms (sfx alto rouba o canal) e retoma sozinho do ponto onde parou. */
        duck(ms: number): boolean;
        mute(on: boolean): void;
        volume(v: number): void;
    };

    // ------------------------------------------------------------ save --
    const save: {
        prefix: string;
        get(key: string, def?: string): string;
        set(key: string, val: any): void;
        num(key: string, def?: number): number;
        best(key: string, score: number): boolean;
    };
}

declare namespace P {
    const version: string;
    interface Vec { x: number; y: number }
    interface Body {
        x: number; y: number;
        r?: number;                    // circulo
        w?: number; h?: number;        // caixa (x,y = centro)
        vx?: number; vy?: number;
        ax?: number; ay?: number;
        gravity?: number;
        bounce?: number;
        friction?: number;
        drag?: number;
        mass?: number;
        static?: boolean;
        sensor?: boolean;
        group?: number;
        mask?: number;
        tiles?: boolean;               // false: ignora o tilemap
        drop?: boolean;                // atravessa plataformas one-way
        grounded?: boolean;
        onCollide?: (me: Body, other: Body, info: { nx: number; ny: number; overlap: number }) => void;
        [k: string]: any;
    }
    interface World {
        gravity: Vec;
        bounds: { x: number; y: number; w: number; h: number } | null;
        walls: 'contain' | 'wrap' | 'none';
        maxSub: number;
        count: number;
        all: Body[];
        tiles: Tiles | null;
        add(b: Body): Body;
        remove(b: Body): void;
        clear(): void;
        each(fn: (b: Body) => void): void;
        addTiles(t: Tiles): Tiles;
        step(dt: number): void;
    }
    interface Tiles {
        tw: number; th: number; cols: number; rows: number;
        grid: any[];
        tileAt(px: number, py: number): string | null;
        solidAt(col: number, row: number): boolean;
        onewayAt(col: number, row: number): boolean;
        setTile(col: number, row: number, ch: any): void;
    }
    function world(opts?: {
        gravity?: Vec;
        bounds?: { x: number; y: number; w: number; h: number };
        walls?: 'contain' | 'wrap' | 'none';
        maxSub?: number;
    }): World;
    function tiles(grid: any[], tw: number, th: number, opts?: {
        solid?: (ch: any) => boolean;
        oneway?: (ch: any) => boolean;
    }): Tiles;
    function hit(a: Body, b: Body): boolean;
    interface VerletPoint { x: number; y: number; px?: number; py?: number; pin?: boolean; [k: string]: any }
    interface VerletStick { a: number; b: number; len?: number }
    interface Verlet {
        points: VerletPoint[];
        sticks: VerletStick[];
        iterations: number;
        stick(a: number, b: number, len?: number): VerletStick;
        stickLen(s: VerletStick): number;
        pin(i: number, on?: boolean): void;
        step(dt: number, o?: { gravity?: Vec; damp?: number;
                               bounds?: { x: number; y: number; w: number; h: number };
                               bounce?: number }): void;
    }
    function verlet(opts?: {
        points?: VerletPoint[];
        sticks?: VerletStick[];
        iterations?: number;
    }): Verlet;
}
