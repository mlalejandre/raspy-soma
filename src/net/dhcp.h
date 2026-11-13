#ifndef SOMA_DHCP_H
#define SOMA_DHCP_H
/* PARCHE 081: pide direccion al router por DHCP y la aplica (IP, mascara, puerta de enlace, DNS). 0 = bien; si falla, se deja la IP de antes. */
int dhcp_run(int verbose);
#endif
