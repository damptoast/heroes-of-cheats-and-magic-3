/*
 * Finds the adventure-map tile grid on a screenshot of the VCMI window, so the
 * pad's grid mouse can step from tile centre to tile centre. VCMI's tile size
 * depends on zoom and UI scaling, and the map scrolls by pixels, so neither the
 * size nor the offset can be known from outside; both are measured instead.
 *
 * Tile edges (VCMI's grid lines when "show grid" is on) repeat every tile, so
 * the summed brightness change per column is periodic. The period is the tile
 * size; the phase of its peaks is where tile edges sit. Same for rows.
 *
 * The tile size is usually fractional (zoom 59 x UI scale / Windows scaling,
 * e.g. 41.5 px), so periods are searched in quarter pixels: a whole-pixel comb
 * drifts off the lines within a few tiles.
 *
 * Included by H3Overlay.c. Build with -DGRID_DEBUG to print the fit.
 */

typedef struct {
    int ok;
    double size;              /* tile size in pixels (x and y agree) */
    double ox, oy;            /* a tile edge column / row, in image pixels */
    double strength;          /* comb peak / mean, for logging */
} GridFit;

#define GRID_MIN 16
#define GRID_MAX 200
#define GRID_STEP 0.25        /* period resolution */
#define GRID_NP 737           /* (GRID_MAX - GRID_MIN) / GRID_STEP + 1 */

/* Comb strength of period p (fractional): how much more edge the best phase
   collects (at round(phase + k*p), +/-1) than the average phase does. Phases
   are tried every half pixel. */
static double grid_comb(const double *e, int n, double p, double *phase) {
    double best = 0, total = 0, ph;
    int count = 0;
    for (ph = 0; ph < p; ph += 0.5) {
        double s = 0, pos;
        for (pos = ph; pos < n; pos += p) {
            int i = (int)(pos + 0.5);
            if (i >= n) break;
            s += e[i];
            if (i > 0) s += e[i - 1] * 0.5;
            if (i + 1 < n) s += e[i + 1] * 0.5;
        }
        total += s;
        count++;
        if (s > best) { best = s; *phase = ph; }
    }
    if (!count) return 0;
    total /= count;
    return total > 0 ? best / total : 0;
}

/* Sum the comb of period p collects at one phase. */
static double grid_comb_at(const double *e, int n, double p, double ph) {
    double s = 0, pos;
    while (ph >= p) ph -= p;
    for (pos = ph; pos < n; pos += p) {
        int i = (int)(pos + 0.5);
        if (i >= n) break;
        s += e[i];
        if (i > 0) s += e[i - 1] * 0.5;
        if (i + 1 < n) s += e[i + 1] * 0.5;
    }
    return s;
}

/* Best period and phase of a 1-D edge profile. Returns the comb strength.
   On a real map, 2 or 4 tiles often comb *better* than one tile (fewer
   samples, so stray scenery lines up more easily). So take the strongest
   period P, then test whether it is really d tiles: if the comb also finds
   edges at every in-between position (phase + j*P/d), the tile is P/d.
   A true tile has lines there; a half tile has none. */
static double grid_axis(const double *e, int n, double *period, double *phase) {
    double best = 0, best_p = 0, best_ph = 0, pmax = n / 3.0 < GRID_MAX ? n / 3.0 : GRID_MAX;
    double p, ph = 0, s, peak;
    int d, j;
    if (pmax < GRID_MIN) return 0;                 /* need 3+ tiles across */
    for (p = GRID_MIN; p <= pmax; p += GRID_STEP) {
        s = grid_comb(e, n, p, &ph);
        if (s > best) { best = s; best_p = p; best_ph = ph; }
    }
    if (!best_p) return 0;
    peak = grid_comb_at(e, n, best_p, best_ph);
    for (d = 5; d >= 2; d--) {
        double q = best_p / d, qph, qs;
        int all = 1;
        if (q < GRID_MIN) continue;
        for (j = 1; j < d && all; j++)
            if (grid_comb_at(e, n, best_p, best_ph + j * q) < peak * 0.6) all = 0;
        if (!all) continue;
        /* refine the small period's phase near the found edge */
        qph = best_ph;
        while (qph >= q) qph -= q;
        qs = grid_comb(e, n, q, &qph);
        *period = q;
        *phase = qph;
        return qs;
    }
    *period = best_p;
    *phase = best_ph;
    return best;
}

/* px: 0x00RRGGBB, w x h, top-down. Measures inside rect r (the map area). */
static GridFit grid_find(const DWORD *px, int w, int h, RECT r) {
    GridFit g;
    double *ex, *ey, sx, sy, px_x = 0, ph_x = 0, px_y = 0, ph_y = 0;
    int x, y, nx, ny;
    memset(&g, 0, sizeof(g));
    if (r.left < 1) r.left = 1;
    if (r.top < 1) r.top = 1;
    if (r.right > w) r.right = w;
    if (r.bottom > h) r.bottom = h;
    nx = (int)(r.right - r.left);
    ny = (int)(r.bottom - r.top);
    if (nx < GRID_MIN * 3 || ny < GRID_MIN * 3) return g;
    ex = (double *)calloc((size_t)nx, sizeof(double));
    ey = (double *)calloc((size_t)ny, sizeof(double));
    if (!ex || !ey) { free(ex); free(ey); return g; }
    for (y = r.top; y < r.bottom; y++) {
        const DWORD *row = px + (size_t)y * w, *up = row - w;
        for (x = r.left; x < r.right; x++) {
            DWORD c = row[x], l = row[x - 1], u = up[x];
            int gc = (int)(((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255));
            int gl = (int)(((l >> 16) & 255) + ((l >> 8) & 255) + (l & 255));
            int gu = (int)(((u >> 16) & 255) + ((u >> 8) & 255) + (u & 255));
            ex[x - r.left] += abs(gc - gl);
            ey[y - r.top] += abs(gc - gu);
        }
    }
    sx = grid_axis(ex, nx, &px_x, &ph_x);
    sy = grid_axis(ey, ny, &px_y, &ph_y);
#ifdef GRID_DEBUG
    printf("x: period %.2f phase %.1f strength %.2f | y: period %.2f phase %.1f strength %.2f\n",
        px_x, ph_x, sx, px_y, ph_y, sy);
#endif
    free(ex);
    free(ey);
    /* trust it only when both axes agree on the tile size and show real peaks */
    if (px_x > 0 && px_y > 0 && fabs(px_x - px_y) <= 1.0 && sx > 1.4 && sy > 1.4) {
        g.ok = 1;
        g.size = (px_x + px_y) / 2;
        g.ox = r.left + ph_x;
        g.oy = r.top + ph_y;
        g.strength = sx < sy ? sx : sy;
    }
    return g;
}

/* Centre of the tile containing (x, y), moved by (dx, dy) tiles. */
static void grid_step_point(const GridFit *g, int x, int y, int dx, int dy, int *tx, int *ty) {
    double ix = floor((x - g->ox) / g->size);
    double iy = floor((y - g->oy) / g->size);
    *tx = (int)floor(g->ox + (ix + dx + 0.5) * g->size + 0.5);
    *ty = (int)floor(g->oy + (iy + dy + 0.5) * g->size + 0.5);
}
