# Referencia independiente (hashlib/hmac + cryptography) para tests/test_wpa.c
import hmac, hashlib, struct
from cryptography.hazmat.primitives.keywrap import aes_key_wrap
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
def prf(k,label,data,n):
    out=b'';i=0
    while len(out)<n: out+=hmac.new(k,label.encode()+b'\0'+data+bytes([i]),hashlib.sha1).digest(); i+=1
    return out[:n]
ssid=b'SOMA-casa'; pw=b'contrasena-segura'
pmk=hashlib.pbkdf2_hmac('sha1',pw,ssid,4096,32)
aa=bytes.fromhex('020000000001'); spa=bytes.fromhex('dca632aabbcc')
an=bytes(range(32)); sn=bytes(range(100,132))
d=min(aa,spa)+max(aa,spa)+min(an,sn)+max(an,sn)
ptk=prf(pmk,'Pairwise key expansion',d,48); kck,kek=ptk[:16],ptk[16:32]
def frame(ki,replay,nonce,data,mic_key=None,ver=2,klen=0):
    body=struct.pack('>BHH',2,ki,klen)+replay+nonce+bytes(16)+bytes(8)+bytes(8)+bytes(16)+struct.pack('>H',len(data))+data
    f=struct.pack('>BBH',ver,3,len(body))+body
    if mic_key:
        mic=hmac.new(mic_key,f,hashlib.sha1).digest()[:16]; f=f[:81]+mic+f[97:]
    return f
ie=bytes([48,20,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0])
r1=(1).to_bytes(8,'big'); r3=(2).to_bytes(8,'big'); r5=(3).to_bytes(8,'big')
m1=frame(2|0x8|0x80,r1,an,b'',ver=2,klen=16)
m2=frame(2|0x8|0x100,r1,sn,ie,kck)
gtk=bytes(range(0xA0,0xB0))
kde=bytes([0xdd,4+2+16,0,0x0f,0xac,1,0x01,0])+gtk
kd=ie+kde; kd+=b'\xdd'+bytes(((8-(len(kd)+1)%8)%8)) if (len(kd))%8 else b''
while len(kd)%8 or len(kd)<16: kd+=b'\0'
m3=frame(2|0x8|0x40|0x80|0x100|0x200|0x1000,r3,an,aes_key_wrap(kek,kd),kck,klen=16)
m4=frame(2|0x8|0x100|0x200,r3,bytes(32),b'',kck)
gtk2=bytes(range(0xC0,0xD0)); kd2=bytes([0xdd,22,0,0x0f,0xac,1,0x02,0])+gtk2
g1=frame(2|0x80|0x100|0x200|0x1000,r5,bytes(32),aes_key_wrap(kek,kd2),kck,klen=16)
g2=frame(2|0x100|0x200,r5,bytes(32),b'',kck)
# escaneo: dos redes en disposicion alineada + una empaquetada
def bss(bssid,ssid,cap,chanspec,rssi,ies,packed=False):
    L=(71,76,109,111,117) if packed else (72,78,116,120,128)
    b=bytearray(L[4]); struct.pack_into('<II',b,0,109,L[4]+len(ies)); b[8:14]=bssid; struct.pack_into('<H',b,16,cap)
    b[18]=len(ssid); b[19:19+len(ssid)]=ssid; struct.pack_into('<H',b,L[0],chanspec); struct.pack_into('<h',b,L[1],rssi)
    struct.pack_into('<H',b,L[2],L[4]); struct.pack_into('<I',b,L[3],len(ies)); return bytes(b)+ies
rsn_psk=bytes([48,20,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0x0c,0])
rsn_mix=bytes([48,24,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,2,0,0,0x0f,0xac,2,0,0x0f,0xac,8,0x8c,0])
b1=bss(bytes.fromhex('aabbccddee01'),b'SOMA-casa',0x0411,0x1006,-48,bytes([0,9])+b'SOMA-casa'+rsn_psk)
b2=bss(bytes.fromhex('aabbccddee02'),b'Vecino5G',0x0011,0xd024,-71,rsn_mix)
b3=bss(bytes.fromhex('aabbccddee03'),b'Libre',0x0001,0x100b,-60,b'',packed=True)
b1w=bss(bytes.fromhex('aabbccddee01'),b'SOMA-casa',0x0411,0x1006,-80,rsn_psk)  # duplicado mas debil
blob=b1+b2+b3+b1w; esc=struct.pack('<IIHH',12+len(blob),109,7,4)+blob
# AES FIPS-197 y RFC 3394
key=bytes(range(16)); pt=bytes.fromhex('00112233445566778899aabbccddeeff')
ct=Cipher(algorithms.AES(key),modes.ECB()).encryptor().update(pt)
def c(name,b): return 'static const uint8_t %s[%d] = {%s};'%(name,len(b),','.join('0x%02x'%x for x in b))
out=[c('PMK',pmk),c('PTK',ptk),c('M1',m1),c('M2',m2),c('M3',m3),c('M4',m4),c('G1',g1),c('G2',g2),c('GTK',gtk),c('GTK2',gtk2),c('ESC',esc),c('AESCT',ct),
     c('PBK_IEEE',hashlib.pbkdf2_hmac('sha1',b'password',b'IEEE',4096,32)),
     c('PRF_REF',prf(bytes([0x0b]*20),'prefix',b'Hi There',64))]
open(__import__('os').path.join(__import__('os').path.dirname(__file__),'wpa_vectors.h'),'w').write('/* PARCHE 080: vectores generados con una referencia independiente (Python: hashlib, hmac y cryptography). No editar a mano. */\n#include <stdint.h>\n'+'\n'.join(out)+'\n')
print(len(m3), pmk.hex(), prf(bytes([0x0b]*20),'prefix',b'Hi There',64).hex()[:32], hashlib.pbkdf2_hmac('sha1',b'password',b'IEEE',4096,32).hex())
