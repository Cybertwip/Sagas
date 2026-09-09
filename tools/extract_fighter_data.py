from pathlib import Path
import re,csv,json
# Run after the US decomp data and Sagas reloc assets have been generated.
r=Path(__file__).resolve().parents[2]; d=r/'ssb-decomp-re'; out=r/'sagas/include/sagas/FighterSourceData.hpp'
ids={v['name']:int(v['id']) for v in csv.DictReader((r/'sagas/build/assets/reloc/manifest.tsv').open(),delimiter='\t')}
voices={v['name']:v['idx'] for v in json.loads((d/'build/us/src/audio/fgm.ucd.json').read_text())['entries']}
names=['Luigi','Mario','Donkey','Link','Samus','Captain','Ness','Yoshi','Kirby','Fox','Pikachu','Purin']
selected=[1,3,1,1,4,1,2,2,3,4,1,2]; scales=[1.21,1.25,1.,1.33,1.03,1.07,1.3,1.05,1.22,1.15,1.2,1.26]
text='// US cartridge values from FTAttributes and scSubsys motion tables.\n#pragma once\n#include <array>\nnamespace sagas {\nstruct FighterSourceData {\n float size,walk_mul,traction,dash,run,kneebend,jump_x,jump_mul,jump_base,air_accel,air_max,air_friction,gravity,terminal,fast,weight,height,width,select_scale,aerial_x,aerial_height,jab_window,cam_offset_y,camera_zoom,dash_decel,dash_to_run;\n unsigned jumps,idle,walk,dash_clip,run_clip,jump,fall,landing,jab,damage,selected,announce,selected_flags,kneebend_clip,jump_back,aerial_forward,aerial_back,jab2,jab3,run_brake;\n std::array<unsigned,5> multi_jump;\n std::array<unsigned,3> smash,rapid,smash_voices;\n std::array<unsigned,5> attack_air,landing_air;\n std::array<unsigned,4> grab;\n std::array<unsigned,3> crouch;\n std::array<unsigned,7> tilt;\n std::array<unsigned,3> walks;\n std::array<float,3> walk_lengths;\n unsigned landing_sfx,down_sfx;\n unsigned dash_attack,turn;\n unsigned capture_joint;\n std::array<unsigned,2> capture;\n std::array<unsigned,3> tech;\n std::array<unsigned,6> special_start,special_loop,special_end,special_active;\n std::array<unsigned,12> down; // Bounce, stand, tech rolls, get-up rolls, attacks (D/U pairs).\n std::array<unsigned,20> damage_reactions; // Common motions DamageHi1 through DamageFall.\n std::array<unsigned,8> cliff;\n std::array<float,2> cliff_box;\n};\ninline constexpr std::array<FighterSourceData,12> fighter_source_data{{\n'
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
 coll=re.findall(r'[-\d.]+',vals['map_coll']);floats+=[coll[0],coll[3],str(scales[idx]),num('jumpaerial_vel_x'),num('jumpaerial_height'),num('attack1_followup_frames'),num('cam_offset_y'),num('camera_zoom'),num('dash_decel'),num('dash_to_run')]
 def clip(*suffix):
  for s in suffix:
   if 'FT'+name+'Anim'+s in ids:return ids['FT'+name+'Anim'+s]
  if name=='Purin' and suffix[0]=='LandingAirX':return 1281
  if name=='Purin' and (suffix[0].startswith('Cliff') or suffix[0]=='Catch' or suffix[0].startswith('Crouch')):return ids['FTKirbyAnim'+suffix[0]]
  if name=='Luigi':
   for s in suffix:
    if 'FTMarioAnim'+s in ids:return ids['FTMarioAnim'+s]
  raise ValueError((name,suffix))
 sub=(d/f'src/sc/scsubsys/scsubsysdata{name.lower()}.c').read_text().split('SubMotionDescs[]')[1]
 subs=re.findall(r'^\s*(?:&ll(\w+)FileID|(0x00000000)),',sub,re.M)
 clips=[int(num('jumps_max')),clip('Wait','Idle','EggLay'),clip('Walk2'),clip('Dash'),clip('Run'),clip('JumpF'),clip('Fall'),clip('LandingAirX'),clip('Jab1','Jab'),clip('Damage'),ids[subs[selected[idx]][0]],voices['nSYAudioVoiceAnnounce'+name]]
 extra=[clip('JumpSquat','LandingAirX'),clip('JumpB'),clip('JumpAerialF','Jump2','JumpAerialB'),clip('JumpAerialB','Jump2')]
 extra += [ids.get('FT'+name+'AnimJab2',ids.get('FTMarioAnimJab2',0) if name=='Luigi' else 0),ids.get('FT'+name+'AnimJab3',ids.get('FTMarioAnimJab3',0) if name=='Luigi' else 0),clip('RunBrake')]
 multi=[clip('Jump'+str(i)) for i in range(2,7)] if name in ('Kirby','Purin') else [0]*5
 attacks=[clip('FSmash'),clip('USmash'),clip('DSmash')]
 rapid=[ids.get('FT'+name+'AnimJabLoop'+suffix,0) for suffix in ('Start','','End')]
 smash_voices=[voices[name] for name in re.findall(r'nSYAudio\w+',vals['smash_sfx'])]
 cliff=[clip(s) for s in ('CliffCatch','CliffWait','CliffQuick','CliffClimbQuick1','CliffClimbQuick2','CliffSlow','CliffClimbSlow1','CliffClimbSlow2')]
 air=[clip('AttackAir'+s) for s in 'NFBUD']
 landing_air=[ids.get('FT'+('Mario' if name=='Luigi' else name)+'AnimLandingAir'+s,0) for s in 'NFBUD']
 grab=[clip('Catch'),clip('CatchPull'),clip('ThrowF','ForwardThrow'),clip('ThrowB')]
 tilt=[ids.get('FT'+name+'Anim'+s,ids.get('FTMarioAnim'+s,0) if name=='Luigi' else 0) for s in ('FTiltHigh','FTiltMidHigh','FTilt','FTiltMidLow','FTiltLow','UTilt','DTilt')]
 walks=[clip('Walk'+str(i)) for i in (1,2,3)]
 walk_lengths=[num(k) for k in ('walkslow_anim_length','walkmiddle_anim_length','walkfast_anim_length')]
 dash_attack=clip('DashAttack');turn=clip('Turn')
 table=re.search(r'FTMotionDesc dFT'+name+r'MotionDescs\[\]\s*=\s*\{(.*?)\n\};',(d/'src/ft/ftdata.c').read_text(),re.S).group(1)
 motions=re.findall(r'^\s*(?:\{\s*&ll(\w+)FileID|\{?\s*0x00000000)\s*,',table,re.M)
 common=(d/'src/ft/ftdef.h').read_text().split('typedef enum FTCommonMotion')[1].split('}')[0]
 common=re.sub(r'//[^\n]*|/\*.*?\*/','',common,flags=re.S)
 enum_values={};value=-1
 for entry in common.split(','):
  m=re.search(r'(nFTCommonMotion\w+)(?:\s*=\s*(-?\d+|nFTCommonMotion\w+))?',entry)
  if not m:continue
  value=(enum_values[m[2]] if m[2].startswith('nFT') else int(m[2])) if m[2] else value+1
  enum_values[m[1]]=value
 special_base=enum_values['nFTCommonMotionSpecialStart']
 header=(d/f'src/ft/ftchar/ft{name.lower()}/ft{name.lower()}.h').read_text()
 enum=re.search(r'typedef enum ft'+name+r'Motion\s*\{(.*?)\}',header,re.S).group(1)
 enum=re.sub(r'//[^\n]*','',enum)
 motion_names=re.findall('nFT'+name+r'Motion(\w+)',enum)
 special_ids={n:ids.get(motions[special_base+i],0) for i,n in enumerate(motion_names) if special_base+i<len(motions)}
 special_start=[];special_loop=[];special_end=[];special_active=[]
 for air_mode in (False,True):
  for direction in ('N','Hi','Lw'):
   stem='Special'+('Air' if air_mode else '')+direction
   if not any(k.startswith(stem) for k in special_ids):stem='Special'+direction
   start=special_ids.get(stem+'Start',special_ids.get(stem,0))
   loop=special_ids.get(stem+'Loop',special_ids.get(stem+'Hold',0))
   end=special_ids.get(stem+'End',0)
   special_active.append(special_ids.get(stem,0))
   special_start.append(start);special_loop.append(loop);special_end.append(end)
 capture=[ids[motions[enum_values['nFTCommonMotion'+n]]] for n in ('CapturePulled','ThrownCommon')]
 tech=[ids[motions[i]] for i in (70,62,63)]
 down=[ids[m] for m in motions[58:70]]
 reactions=[ids[m] for m in motions[31:51]]
 assert len(reactions)==20
 crouch=[clip('Crouch'),clip('CrouchIdle'),clip('CrouchEnd')]
 cliff_box=re.findall(r'[-\d.]+',vals['cliffcatch_coll'])
 flags=re.findall(r'^\s*(?:&ll\w+FileID|0x00000000),\s*[^,]+,\s*(0x[0-9A-Fa-f]+)',sub,re.M)[selected[idx]]
 text+='    {'+','.join(v+'f' if '.' in v else v+'.0f' for v in floats)+','+','.join(map(str,clips))+','+flags+'U,'+','.join(map(str,extra))+',{'+','.join(map(str,multi))+'},{'+','.join(map(str,attacks))+'},{'+','.join(map(str,rapid))+'},{'+','.join(map(str,smash_voices))+'},{'+','.join(map(str,air))+'},{'+','.join(map(str,landing_air))+'},{'+','.join(map(str,grab))+'},{'+','.join(map(str,crouch))+'},{'+','.join(map(str,tilt))+'},{'+','.join(map(str,walks))+'},{'+','.join(v+'f' for v in walk_lengths)+'},'+str(voices['nSYAudioFGM'+('Mario' if name=='Luigi' else name)+'Landing'])+','+str(voices['nSYAudioFGM'+('Mario' if name=='Luigi' else name)+'DownBounce'])+','+str(dash_attack)+','+str(turn)+','+num('joint_itemheavy_id')+',{'+','.join(map(str,capture))+'},{'+','.join(map(str,tech))+'},{'+','.join(map(str,special_start))+'},{'+','.join(map(str,special_loop))+'},{'+','.join(map(str,special_end))+'},{'+','.join(map(str,special_active))+'},{'+','.join(map(str,down))+'},{'+','.join(map(str,reactions))+'},{'+','.join(map(str,cliff))+'},{'+','.join(v+'f' for v in cliff_box)+'}}, // '+name+'\n'
text+='}};\n'
# Main-motion flags bind both wrapper tracks and auxiliary joints (e.g. grabs).
macros={k:int(v,16) for k,v in re.findall(r'#define\s+(FTANIM_FLAG_\w+)\s+(0x[0-9A-Fa-f]+)',(d/'src/ft/ftdef.h').read_text())}
motion_flags={}
for name,flags in re.findall(r'\{\s*&ll(\w+)FileID,\s*[^,]+,\s*([^}]+)\}',(d/'src/ft/ftdata.c').read_text()):
 if name not in ids:continue
 value=0
 for token in re.findall(r'FTANIM_FLAG_\w+|0x[0-9A-Fa-f]+',flags):value|=macros.get(token,0) if token.startswith('FT') else int(token,16)
 motion_flags.setdefault(ids[name],value)
text+='struct SourceAnimationFlags { unsigned motion,flags; };\n'
text+=f'inline constexpr std::array<SourceAnimationFlags,{len(motion_flags)}> source_animation_flags{{{{\n'
text+=''.join('    {'+str(clip)+','+hex(flags)+'U},\n' for clip,flags in sorted(motion_flags.items()))+'}};\n'
text+='inline unsigned fighter_motion_flags(unsigned motion) { for (const auto& entry:source_animation_flags) if (entry.motion==motion) return entry.flags; return 0; }\n}\n'
out.write_text(text)
