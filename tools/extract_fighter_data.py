from pathlib import Path
import re,csv,json
# Run after the US decomp data and Sagas reloc assets have been generated.
r=Path(__file__).resolve().parents[2]; d=r/'ssb-decomp-re'; out=r/'sagas/include/sagas/FighterSourceData.hpp'
ids={v['name']:int(v['id']) for v in csv.DictReader((r/'sagas/build/assets/reloc/manifest.tsv').open(),delimiter='\t')}
voices={v['name']:v['idx'] for v in json.loads((d/'build/us/src/audio/fgm.ucd.json').read_text())['entries']}
names=['Luigi','Mario','Donkey','Link','Samus','Captain','Ness','Yoshi','Kirby','Fox','Pikachu','Purin']
selected=[1,3,1,1,4,1,2,2,3,4,1,2]; scales=[1.21,1.25,1.,1.33,1.03,1.07,1.3,1.05,1.22,1.15,1.2,1.26]
text='// US cartridge values from FTAttributes and scSubsys motion tables.\n#pragma once\n#include <array>\nnamespace sagas {\nstruct FighterSourceData {\n float size,walk_mul,traction,dash,run,kneebend,jump_x,jump_mul,jump_base,air_accel,air_max,air_friction,gravity,terminal,fast,weight,height,width,select_scale,aerial_x,aerial_height,jab_window,cam_offset_y,camera_zoom;\n unsigned jumps,idle,walk,dash_clip,run_clip,jump,fall,landing,jab,damage,selected,announce,selected_flags,kneebend_clip,jump_back,aerial_forward,aerial_back,jab2,jab3;\n std::array<unsigned,5> multi_jump;\n std::array<unsigned,3> smash,rapid;\n};\ninline constexpr std::array<FighterSourceData,12> fighter_source_data{{\n'
for idx,name in enumerate(names):
 p=next((d/'src/relocData').glob('[0-9]*_'+name+'Main.c'))
 src=re.search(r'FTAttributes\s+\w+\s*=\s*\{(.*?)\n\};',p.read_text(),re.S).group(1)
 active=[True];lines=[]
 for line in src.splitlines():
  if line.startswith('#if'):active.append(active[-1] and 'REGION_US' in line)
  elif line.startswith('#else') and len(active)>1:active[-1]=active[-2] and not active[-1]
  elif line.startswith('#endif') and len(active)>1:active.pop()
  elif all(active):lines.append(line)
 src='\n'.join(lines)
 vals={k:v.strip().rstrip(',') for v,k in re.findall(r'^\s*([^\n]+?),\s*/\* (\w+) \*/',src,re.M)}
 def num(k):return vals[k].rstrip('fF')
 floats=[num(k) for k in ['size','walk_speed_mul','traction','dash_speed','run_speed','kneebend_anim_length','jump_vel_x','jump_height_mul','jump_height_base','air_accel','air_speed_max_x','air_friction','gravity','tvel_base','tvel_fast','weight']]
 coll=re.findall(r'[-\d.]+',vals['map_coll']);floats+=[coll[0],coll[3],str(scales[idx]),num('jumpaerial_vel_x'),num('jumpaerial_height'),num('attack1_followup_frames'),num('cam_offset_y'),num('camera_zoom')]
 def clip(*suffix):
  for s in suffix:
   if 'FT'+name+'Anim'+s in ids:return ids['FT'+name+'Anim'+s]
  if name=='Purin' and suffix[0]=='LandingAirX':return 1281
  if name=='Luigi':
   for s in suffix:
    if 'FTMarioAnim'+s in ids:return ids['FTMarioAnim'+s]
  raise ValueError((name,suffix))
 sub=(d/f'src/sc/scsubsys/scsubsysdata{name.lower()}.c').read_text().split('SubMotionDescs[]')[1]
 subs=re.findall(r'^\s*(?:&ll(\w+)FileID|(0x00000000)),',sub,re.M)
 clips=[int(num('jumps_max')),clip('Wait','Idle','EggLay'),clip('Walk2'),clip('Dash'),clip('Run'),clip('JumpF'),clip('Fall'),clip('LandingAirX'),clip('Jab1','Jab'),clip('Damage'),ids[subs[selected[idx]][0]],voices['nSYAudioVoiceAnnounce'+name]]
 extra=[clip('JumpSquat','LandingAirX'),clip('JumpB'),clip('JumpAerialF','Jump2','JumpAerialB'),clip('JumpAerialB','Jump2')]
 extra += [ids.get('FT'+name+'AnimJab2',ids.get('FTMarioAnimJab2',0) if name=='Luigi' else 0),ids.get('FT'+name+'AnimJab3',ids.get('FTMarioAnimJab3',0) if name=='Luigi' else 0)]
 multi=[clip('Jump'+str(i)) for i in range(2,7)] if name in ('Kirby','Purin') else [0]*5
 attacks=[clip('FSmash'),clip('USmash'),clip('DSmash')]
 rapid=[ids.get('FT'+name+'AnimJabLoop'+suffix,0) for suffix in ('Start','','End')]
 flags=re.findall(r'^\s*(?:&ll\w+FileID|0x00000000),\s*[^,]+,\s*(0x[0-9A-Fa-f]+)',sub,re.M)[selected[idx]]
 text+='    {'+','.join(v+'f' if '.' in v else v+'.0f' for v in floats)+','+','.join(map(str,clips))+','+flags+'U,'+','.join(map(str,extra))+',{'+','.join(map(str,multi))+'},{'+','.join(map(str,attacks))+'},{'+','.join(map(str,rapid))+'}}, // '+name+'\n'
text+='}};\n}\n';out.write_text(text)
