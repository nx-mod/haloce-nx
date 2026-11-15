"""The real settings senders/reassembler share one bounded record for every map family."""
from pathlib import Path

from harness import function
from c_runner import run_c

ROOT = Path(__file__).resolve().parents[1]


def test_shared_settings_roundtrip_and_rejection(tmp_path):
    receiver = (ROOT / "source/networking/network_client_message_handler.c").read_text()
    sender = (ROOT / "source/networking/network_server_message_handler.c").read_text()
    code = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
typedef unsigned char byte;
typedef unsigned short word;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define csmemset memset
#define csmemcpy memcpy
#define match_assert(file,line,value) assert(value)
#define network_event(...) ((void)0)
#define system_milliseconds() 1
enum {_message_server_game_settings_update};
struct network_game {struct {char name[64];} map;byte mcc_content_sha256[32],mcc_metadata_sha256[32];};
struct network_game_client {int unused;};
struct network_game_server {int unused;};
struct network_game_server_client_machine {int unused;};
struct message_server_game_settings_update {word total_size,offset,length,pad;byte data[48];};
static struct network_game network_game_client_settings_staging, applied_game;
static long network_game_client_settings_staging_size, network_game_client_settings_staging_total;
static boolean network_game_settings_update_pending;
static unsigned long network_game_settings_update_time;
static int applied, sent, fail_send;
static int network_game_client_game_settings_updated(struct network_game_client *c,struct network_game const *g)
{(void)c;applied++;applied_game=*g;return 1;}
static void const *mcc_identity_host_settings(void const *record,long size,struct network_game *out)
{if(size!=sizeof(*out))return NULL;memcpy(out,record,size);return out;}
''' + function(receiver, "network_game_client_reset_settings_staging") + "\n"
    code += function(receiver, "network_game_client_receive_game_settings_piece") + r'''
static void *create_network_game_message(int kind,void *message,long size)
{assert(kind==_message_server_game_settings_update);assert(size==sizeof(struct message_server_game_settings_update));return message;}
static int deliver(void *message)
{
 struct message_server_game_settings_update *piece=message;
 assert(piece->total_size==sizeof(struct network_game));
 sent++;if(fail_send==sent)return 0;
 return network_game_client_receive_game_settings_piece(NULL,piece);
}
static int network_game_server_send_message_to_client_machine(struct network_game_server *s,
 struct network_game_server_client_machine *m,void *message) {(void)s;(void)m;return deliver(message);}
static int network_game_server_send_message_to_all_machines(struct network_game_server *s,void *message)
{(void)s;return deliver(message);}
'''
    for name in ("network_game_server_send_game_settings_to_client_machine", "network_game_server_send_game_settings_to_all_machines"):
        code += function(sender, name) + "\n"
    code += r'''
int main(void)
{
 struct network_game game;
 struct network_game_server server;
 struct message_server_game_settings_update piece;
 char const *names[]={"bloodgulch","custom_maps\\deathisland","mcc_maps\\package\\a10"};
 int family,broadcast,old_applied;
 for(family=0;family<3;family++)for(broadcast=0;broadcast<2;broadcast++) {
  memset(&game,0,sizeof(game));strcpy(game.map.name,names[family]);
  if(family==2){memset(game.mcc_content_sha256,0x13,32);memset(game.mcc_metadata_sha256,0x47,32);}
  old_applied=applied;sent=0;
  assert(broadcast ? network_game_server_send_game_settings_to_all_machines(&server,&game,sizeof(game)) :
   network_game_server_send_game_settings_to_client_machine(&server,NULL,&game,sizeof(game)));
  assert(sent==3 && applied==old_applied+1 && !memcmp(&game,&applied_game,sizeof(game)));
 }
 old_applied=applied;
 memset(&piece,0,sizeof(piece));piece.total_size=sizeof(game);piece.length=48;
 strcpy((char *)piece.data,"mcc_maps\\package\\a10");
 assert(network_game_client_receive_game_settings_piece(NULL,&piece));
 network_game_client_reset_settings_staging();
 piece.offset=48;assert(network_game_client_receive_game_settings_piece(NULL,&piece));
 assert(applied==old_applied);
 piece.offset=0;piece.total_size=64;
 assert(!network_game_client_receive_game_settings_piece(NULL,&piece));
 piece.total_size=sizeof(game);piece.length=0;
 assert(!network_game_client_receive_game_settings_piece(NULL,&piece));
 piece.length=49;assert(!network_game_client_receive_game_settings_piece(NULL,&piece));
 piece.offset=127;piece.length=2;assert(!network_game_client_receive_game_settings_piece(NULL,&piece));
 /* A complete unterminated name never reaches the string consumers. */
 memset(&game,0xff,sizeof(game));sent=0;
 assert(!network_game_server_send_game_settings_to_all_machines(&server,&game,sizeof(game)));
 assert(applied==old_applied && network_game_settings_update_pending);
 memset(&game,0,sizeof(game));strcpy(game.map.name,"bloodgulch");sent=0;fail_send=2;
 assert(!network_game_server_send_game_settings_to_all_machines(&server,&game,sizeof(game)));
 assert(sent==3 && applied==old_applied && network_game_settings_update_pending);
 sent=0;fail_send=0;
 assert(network_game_server_send_game_settings_to_all_machines(&server,&game,sizeof(game)));
 assert(applied==old_applied+1 && !network_game_settings_update_pending);
 return 0;
}
'''
    run_c(tmp_path, code)
