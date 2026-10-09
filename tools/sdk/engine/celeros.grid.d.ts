// celeros.grid.d.ts — tipos da dep celeros.grid (utilidades de grade).
// Dep do hub (app.json "deps"): "celeros.grid": "^1.0.0".

declare namespace GRID {
    const version: string;
    /** PRNG deterministico (mesmo esquema do E.rng). */
    function rng(seed: number): () => number;
    /** Grade rows x cols preenchida com ch (arrays — mute com grid[r][c]). */
    function nova(cols: number, rows: number, ch: string): string[][];
    interface Classica {
        grid: string[][];
        /** Celulas que receberam '%'. */
        macios: { c: number; r: number }[];
    }
    /** Arena classica de bomberman. opts { dens=0.55, protege=[[1,1]], respira=true }. */
    function classica(cols: number, rows: number, rng: () => number, opts?: {
        dens?: number;
        protege?: number[][];
        respira?: boolean;
    }): Classica;
    interface Flood {
        ok: number[][];     // 1 nas celulas alcancaveis
        quantos: number;
    }
    /** Conjunto alcancavel a pe de (sc,sr); passavel(ch) decide o que atravessa. */
    function flood(grid: string[][], sc: number, sr: number,
                   passavel: (ch: string) => boolean): Flood;
    /** Primeiro candidato [{c,r}] alcancavel explodindo macios (nao-'#'). */
    function escolheAlcancavel(grid: string[][], candidatos: { c: number; r: number }[],
                               sc: number, sr: number): { c: number; r: number } | null;
    /** Sorteia celula que passa no filtro (default ch === '.'). */
    function celulaLivre(grid: string[][], rng: () => number,
                         filtro?: (c: number, r: number, ch: string) => boolean): { c: number; r: number } | null;
    function contar(grid: string[][], ch: string): number;
}
