#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ncurses.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>

#define MAX_PROCS 512
#define NAME_LEN  64

/* Modos de ordenamiento */
typedef enum { SORT_CPU, SORT_MEM, SORT_PID } SortMode;

/* Estructura de proceso */
typedef struct {
    int    pid;
    char   name[NAME_LEN];
    long   mem_kb;
    double cpu_pct;
    char   state;
} Process;

/* ── Filtro de búsqueda ── */
static char my_filter[NAME_LEN] = "";
static int  filter_mode = 0;   /* 0=off, 1=escribiendo */

/* ── Modo de orden actual ── */
static SortMode sort_mode = SORT_CPU;

/* Lee memoria de /proc/PID/status */
long read_mem(int pid) {
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    long kb = 0;
    while (fgets(line, sizeof(line), f))
        if (strncmp(line, "VmRSS:", 6) == 0) { sscanf(line+6, " %ld", &kb); break; }
    fclose(f);
    return kb;
}

/* Lee stat de /proc/PID/stat */
int read_stat(int pid, char *name, char *state,
              unsigned long *utime, unsigned long *stime) {
    char path[64], buf[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; }
    fclose(f);
    char *s = strchr(buf, '('), *e = strrchr(buf, ')');
    if (!s || !e) return 0;
    int len = e - s - 1;
    if (len >= NAME_LEN) len = NAME_LEN - 1;
    strncpy(name, s+1, len); name[len] = '\0';
    int di; long dl; unsigned long dul;
    sscanf(e+2, "%c %d %d %d %d %d %lu %lu %lu %lu %lu %lu",
           state, &di,&di,&di,&di,&di, &dul,&dul,&dul,&dul, utime, stime);
    return 1;
}

/* Comparadores */
int cmp_cpu(const void *a, const void *b) {
    double d = ((Process*)b)->cpu_pct - ((Process*)a)->cpu_pct;
    return (d>0)-(d<0);
}
int cmp_mem(const void *a, const void *b) {
    return (int)(((Process*)b)->mem_kb - ((Process*)a)->mem_kb);
}
int cmp_pid(const void *a, const void *b) {
    return ((Process*)a)->pid - ((Process*)b)->pid;
}

/* Dibuja una barra ASCII de 'value' sobre 'max', ancho 'width' */
void draw_bar(int row, int col, double value, double max, int width, int color_pair) {
    int filled = (max > 0) ? (int)((value / max) * width) : 0;
    if (filled > width) filled = width;
    attron(COLOR_PAIR(color_pair));
    mvaddch(row, col, '[');
    for (int i = 0; i < width; i++)
        addch(i < filled ? '|' : ' ');
    addch(']');
    attroff(COLOR_PAIR(color_pair));
}

int main(void) {
    initscr(); noecho(); curs_set(0); timeout(1000);
    start_color();
    init_pair(1, COLOR_BLACK,  COLOR_CYAN);    /* encabezado */
    init_pair(2, COLOR_CYAN,   COLOR_BLACK);   /* normal */
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);   /* CPU alta */
    init_pair(4, COLOR_WHITE,  COLOR_BLACK);   /* normal */
    init_pair(5, COLOR_GREEN,  COLOR_BLACK);   /* barra baja */
    init_pair(6, COLOR_RED,    COLOR_BLACK);   /* barra alta */
    init_pair(7, COLOR_BLACK,  COLOR_YELLOW);  /* filtro activo */

    while (1) {
        /* ── Leer procesos ── */
        Process procs[MAX_PROCS];
        int count = 0;
        DIR *dir = opendir("/proc");
        struct dirent *entry;
        while ((entry = readdir(dir)) && count < MAX_PROCS) {
            if (!isdigit(entry->d_name[0])) continue;
            int pid = atoi(entry->d_name);
            unsigned long ut=0, st=0; char state='?';
            if (!read_stat(pid, procs[count].name, &state, &ut, &st)) continue;
            procs[count].pid     = pid;
            procs[count].state   = state;
            procs[count].mem_kb  = read_mem(pid);
            procs[count].cpu_pct = (double)(ut+st) / sysconf(_SC_CLK_TCK);
            count++;
        }
        closedir(dir);

        /* ── Aplicar filtro por nombre ── */
        Process filtered[MAX_PROCS];
        int fcount = 0;
        for (int i = 0; i < count; i++) {
            if (strlen(my_filter) == 0 ||
                strstr(procs[i].name, my_filter) != NULL)
                filtered[fcount++] = procs[i];
        }

        /* ── Ordenar según modo actual ── */
        if      (sort_mode == SORT_CPU) qsort(filtered, fcount, sizeof(Process), cmp_cpu);
        else if (sort_mode == SORT_MEM) qsort(filtered, fcount, sizeof(Process), cmp_mem);
        else                            qsort(filtered, fcount, sizeof(Process), cmp_pid);

        /* ── Calcular máximos para las barras ── */
        double max_cpu = 0.01;
        long   max_mem = 1;
        for (int i = 0; i < fcount; i++) {
            if (filtered[i].cpu_pct > max_cpu) max_cpu = filtered[i].cpu_pct;
            if (filtered[i].mem_kb  > max_mem) max_mem = filtered[i].mem_kb;
        }

        /* ── Dibujar ── */
        clear();
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int bar_w = 10;   /* ancho de cada barra */

        /* Encabezado */
        attron(COLOR_PAIR(1)|A_BOLD);
        mvprintw(0, 0, " %-6s %-18s %7s %-*s %9s %-*s %4s",
                 "PID","NOMBRE","CPU(s)",bar_w+2,"CPU","MEM(KB)",bar_w+2,"MEM","EST");
        for (int c = 58; c < cols; c++) addch(' ');

        /* Indicador de orden activo */
        const char *sort_label =
            sort_mode==SORT_CPU ? "[CPU]" :
            sort_mode==SORT_MEM ? "[MEM]" : "[PID]";
        mvprintw(0, cols-14, " Orden:%-7s", sort_label);
        attroff(COLOR_PAIR(1)|A_BOLD);

        /* Filas de procesos */
        int max_rows = rows - 4;
        for (int i = 0; i < fcount && i < max_rows; i++) {
            int cp = (filtered[i].cpu_pct > max_cpu*0.5) ? 3 : 4;
            attron(COLOR_PAIR(cp));
            mvprintw(i+1, 0, " %-6d %-18s %7.2f ",
                     filtered[i].pid, filtered[i].name, filtered[i].cpu_pct);
            attroff(COLOR_PAIR(cp));

            /* Barra CPU */
            int cpu_color = (filtered[i].cpu_pct > max_cpu*0.5) ? 6 : 5;
            draw_bar(i+1, 33, filtered[i].cpu_pct, max_cpu, bar_w, cpu_color);

            attron(COLOR_PAIR(cp));
            printw(" %9ld ", filtered[i].mem_kb);
            attroff(COLOR_PAIR(cp));

            /* Barra MEM */
            int mem_color = (filtered[i].mem_kb > max_mem/2) ? 6 : 5;
            draw_bar(i+1, 33+bar_w+2+10+1, (double)filtered[i].mem_kb,
                     (double)max_mem, bar_w, mem_color);

            attron(COLOR_PAIR(cp));
            printw(" %4c", filtered[i].state);
            attroff(COLOR_PAIR(cp));
        }

        /* Línea de filtro (si está activo) */
        if (filter_mode || strlen(my_filter)>0) {
            attron(COLOR_PAIR(7)|A_BOLD);
            mvprintw(rows-3, 0, " Filtro: %-30s (Enter=aplicar  Esc=limpiar)", my_filter);
            for (int c = 65; c < cols; c++) addch(' ');
            attroff(COLOR_PAIR(7)|A_BOLD);
        }

        /* Barra de ayuda */
        attron(COLOR_PAIR(1));
        mvprintw(rows-2, 0,
            " [q]Salir [k]Matar [/]Filtrar [c]CPU [m]MEM [p]PID | Mostrando:%d/%d",
            fcount, count);
        for (int c = 70; c < cols; c++) addch(' ');
        attroff(COLOR_PAIR(1));

        refresh();

        /* ── Entrada del usuario ── */
        int ch = getch();

        if (filter_mode) {
            if (ch == 27) {                          /* Esc — limpiar filtro */
                memset(my_filter, 0, sizeof(my_filter));
                filter_mode = 0;
            } else if (ch == '\n' || ch == KEY_ENTER) {
                filter_mode = 0;                     /* Enter — aplicar */
            } else if ((ch == KEY_BACKSPACE || ch == 127) && strlen(my_filter)>0) {
                my_filter[strlen(my_filter)-1] = '\0';     /* Borrar */
            } else if (isprint(ch) && strlen(my_filter)<NAME_LEN-1) {
                int l = strlen(my_filter);
                my_filter[l] = (char)ch;
                my_filter[l+1] = '\0';
            }
            continue;
        }

        if (ch == 'q' || ch == 'Q') break;

        if (ch == 'c' || ch == 'C') sort_mode = SORT_CPU;
        if (ch == 'm' || ch == 'M') sort_mode = SORT_MEM;
        if (ch == 'p' || ch == 'P') sort_mode = SORT_PID;

        if (ch == '/') {
            filter_mode = 1;
            memset(my_filter, 0, sizeof(my_filter));
        }

        if (ch == 'k' || ch == 'K') {
            echo(); curs_set(1);
            mvprintw(rows-1, 0, " PID a terminar: ");
            char buf[16] = {0};
            getnstr(buf, sizeof(buf)-1);
            noecho(); curs_set(0);
            int kpid = atoi(buf);
            if (kpid > 1) kill(kpid, SIGTERM);
        }
    }

    endwin();
    printf("procmon terminado.\n");
    return 0;
}
