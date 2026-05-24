#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <dirent.h>
#include <ncurses.h> /*para crear interfaces graficas directamente en la terminal de texto*/
#include <signal.h>
#include <ctype.h>
#include <unistd.h> /*libreria estandar psara tipos y funciones de sistemas Unix */

#define MAX_PROCS 512
#define NAME_LEN 64

/*Estructura< para un proceso */
typedef struct {
    int    pid;
    char   name[NAME_LEN];
    long   mem_kb;
    double cpu_pct;
    char   state;
} Process;

/* Leer memoria de un proceso : /proc/PID/status */
long read_mem(int pid) {
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    long kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, " %ld", &kb);
            break;
        }
    }
    fclose(f);
    return kb;
}

/* Lee el estado y nombre de /proc/PID/stat */
int read_stat(int pid, char *name, char *state, unsigned long *utime, unsigned long *stime) {
    char path[64], buf[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; }
    fclose(f);

    /* El nombre está entre paréntesis */
    char *s = strchr(buf, '(');
    char *e = strrchr(buf, ')');
    if (!s || !e) return 0;
    int len = e - s - 1;
    if (len >= NAME_LEN) len = NAME_LEN - 1;
    strncpy(name, s + 1, len);
    name[len] = '\0';

    /* Estado y tiempos de CPU después del paréntesis */
    int dummy_i; long dummy_l; unsigned long dummy_ul;
    sscanf(e + 2, "%c %d %d %d %d %d %lu %lu %lu %lu %lu %lu",
           state, &dummy_i, &dummy_i, &dummy_i, &dummy_i, &dummy_i,
           &dummy_ul, &dummy_ul, &dummy_ul, &dummy_ul, utime, stime);
    return 1;
}

/* Comparador para ordenar por CPU descendente */
int cmp_cpu(const void *a, const void *b) {
    double diff = ((Process*)b)->cpu_pct - ((Process*)a)->cpu_pct;
    return (diff > 0) - (diff < 0);
}

int main(void) {
    initscr();
    noecho();
    curs_set(0);
    timeout(1000);   /* espera tecla 1 segundo, luego refresca */
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_CYAN);   /* encabezado */
    init_pair(2, COLOR_CYAN,  COLOR_BLACK);  /* PID */
    init_pair(3, COLOR_YELLOW, COLOR_BLACK); /* CPU alta */
    init_pair(4, COLOR_WHITE,  COLOR_BLACK); /* normal */

    while (1) {
        /* --- Leer todos los procesos de /proc --- */
        Process procs[MAX_PROCS];
        int count = 0;

        DIR *dir = opendir("/proc");
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && count < MAX_PROCS) {
            /* Solo nos interesan carpetas con nombre numérico */
            if (!isdigit(entry->d_name[0])) continue;
            int pid = atoi(entry->d_name);

            unsigned long utime = 0, stime = 0;
            char state = '?';
            if (!read_stat(pid, procs[count].name, &state, &utime, &stime))
                continue;

            procs[count].pid    = pid;
            procs[count].state  = state;
            procs[count].mem_kb = read_mem(pid);
            /* CPU: suma de tiempo usuario + sistema, dividido entre ticks del sistema */
            procs[count].cpu_pct = (double)(utime + stime) / sysconf(_SC_CLK_TCK);
            count++;
        }
        closedir(dir);

        /* Ordenar por CPU */
        qsort(procs, count, sizeof(Process), cmp_cpu);

        /* --- Dibujar en pantalla con ncurses --- */
        clear();
        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        /* Encabezado */
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, 0, " %-6s %-20s %8s %10s %5s",
                 "PID", "NOMBRE", "CPU(s)", "MEM(KB)", "EST");
        /* Rellena el resto de la línea */
        for (int c = 47; c < cols; c++) addch(' ');
        attroff(COLOR_PAIR(1) | A_BOLD);

        /* Título arriba a la derecha */
        attron(A_BOLD);
        mvprintw(0, cols - 14, " [procmon v1] ");
        attroff(A_BOLD);

        /* Filas de procesos */
        int max_rows = rows - 3;
        for (int i = 0; i < count && i < max_rows; i++) {
            if (procs[i].cpu_pct > 1.0)
                attron(COLOR_PAIR(3));
            else
                attron(COLOR_PAIR(4));

            mvprintw(i + 1, 0, " %-6d %-20s %8.2f %10ld %5c",
                     procs[i].pid,
                     procs[i].name,
                     procs[i].cpu_pct,
                     procs[i].mem_kb,
                     procs[i].state);
            attroff(COLOR_PAIR(3) | COLOR_PAIR(4));
        }

        /* Barra de ayuda abajo */
        attron(COLOR_PAIR(1));
        mvprintw(rows - 1, 0, " [q] Salir   [k] Terminar proceso   Procesos: %d", count);
        for (int c = 40; c < cols; c++) addch(' ');
        attroff(COLOR_PAIR(1));

        refresh();

        /* --- Esperar tecla --- */
        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;

        if (ch == 'k' || ch == 'K') {
            /* Pedir PID para matar */
            echo();
            curs_set(1);
            mvprintw(rows - 2, 0, " Escribe el PID a terminar: ");
            char pid_buf[16] = {0};
            getnstr(pid_buf, sizeof(pid_buf) - 1);
            noecho();
            curs_set(0);
            int kill_pid = atoi(pid_buf);
            if (kill_pid > 1) {
                kill(kill_pid, SIGTERM);
            }
        }
    }

    endwin();
    printf("procmon terminado.\n");
    return 0;
}

















































































