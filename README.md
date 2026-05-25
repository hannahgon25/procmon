# procmon — Monitor de Procesos del Sistema

Monitor de procesos en tiempo real para Linux, desarrollado en C para Raspberry Pi 4.

## Compilar
make

## Ejecutar
./procmon

## Controles
| Tecla | Acción |
|-------|--------|
| q | Salir |
| k | Terminar proceso (pide PID) |
| c | Ordenar por CPU |
| m | Ordenar por memoria |
| p | Ordenar por PID |
| / | Filtrar por nombre (Esc para limpiar) |

## Tecnologías
- Lenguaje: C
- Librería: ncurses
- Sistema: Raspberry Pi OS (Linux)
- Hardware: Raspberry Pi 4
