#!/usr/bin/env python3
"""Small self-authored skinned GLB fixtures, no downloads or external assets."""
import json, struct, pathlib, sys, zlib

def generate(path, clips=False):
    blob=bytearray(); views=[]; accessors=[]
    def raw(data):
        while len(blob)%4: blob.append(0)
        off=len(blob);blob.extend(data);views.append({'buffer':0,'byteOffset':off,'byteLength':len(data)})
        return len(views)-1
    def acc(rows,fmt,component,kind,normalized=False):
        data=b''.join(struct.pack('<'+fmt,*r) for r in rows);view=raw(data)
        a={'bufferView':view,'componentType':component,'count':len(rows),'type':kind}
        if normalized:a['normalized']=True
        if component==5126 and kind=='VEC3':
            a['min']=[min(r[i] for r in rows) for i in range(3)];a['max']=[max(r[i] for r in rows) for i in range(3)]
        accessors.append(a);return len(accessors)-1
    bones=[('Hips',-1,(0,.95,0)),('Spine',0,(0,.22,0)),('Chest',1,(0,.2,0)),('Neck',2,(0,.2,0)),('Head',3,(0,.12,0)),
           ('LeftUpperLeg',0,(.16,-.06,0)),('LeftLowerLeg',5,(0,-.43,0)),('LeftFoot',6,(0,-.42,-.06)),
           ('RightUpperLeg',0,(-.16,-.06,0)),('RightLowerLeg',8,(0,-.43,0)),('RightFoot',9,(0,-.42,-.06)),
           ('LeftShoulder',2,(.14,.12,0)),('LeftUpperArm',11,(.13,0,0)),('LeftLowerArm',12,(.3,0,0)),('LeftHand',13,(.27,0,0)),
           ('RightShoulder',2,(-.14,.12,0)),('RightUpperArm',15,(-.13,0,0)),('RightLowerArm',16,(-.3,0,0)),('RightHand',17,(-.27,0,0))]
    glob=[];nodes=[{'name':'Body','mesh':0,'skin':0}]
    for i,(name,parent,p) in enumerate(bones):
        glob.append(tuple(p[k]+(glob[parent][k] if parent>=0 else 0) for k in range(3)))
        node={'name':f'Joint{i}' if clips else name,'translation':p}
        children=[j+1 for j,(_,q,_) in enumerate(bones) if q==i]
        if children:node['children']=children
        nodes.append(node)
    positions=[];normals=[];uv=[];joints=[];weights=[];indices=[];morph=[]
    def box(bone,lo,hi):
        center=glob[bone]
        vertices=[tuple(center[k]+(hi[k] if bit&(1<<k) else lo[k]) for k in range(3)) for bit in range(8)]
        faces=[([0,4,6,2],(-1,0,0)),([1,3,7,5],(1,0,0)),([0,1,5,4],(0,-1,0)),([2,6,7,3],(0,1,0)),([0,2,3,1],(0,0,-1)),([4,5,7,6],(0,0,1))]
        for face,normal in faces:
            start=len(positions)
            for vi,tex in zip(face,[(0,0),(0,1),(1,1),(1,0)]):
                positions.append(vertices[vi]);normals.append(normal);uv.append(tex);joints.append((bone,0,0,0));weights.append((1,0,0,0));morph.append((.06 if bone==4 else 0,0,0))
            indices.extend([(start+x,) for x in (0,1,2,0,2,3)])
    box(0,(-.23,-.08,-.13),(.23,.15,.13));box(2,(-.24,-.19,-.13),(.24,.13,.13));box(4,(-.16,-.03,-.16),(.16,.22,.16))
    for a,b,c in [(5,6,7),(8,9,10)]:
        box(a,(-.075,-.42,-.07),(.075,.02,.07));box(b,(-.065,-.40,-.065),(.065,.02,.065));box(c,(-.075,-.04,-.10),(.075,.06,.16))
    for upper,lower,hand,sign in [(12,13,14,1),(16,17,18,-1)]:
        lo,hi=sorted([0,.29*sign]);box(upper,(lo,-.055,-.055),(hi,.055,.055))
        lo,hi=sorted([0,.26*sign]);box(lower,(lo,-.05,-.05),(hi,.05,.05));box(hand,(-.06,-.04,-.075),(.06,.04,.075))
    pos=acc(positions,'3f',5126,'VEC3');nor=acc(normals,'3f',5126,'VEC3');tex=acc(uv,'2f',5126,'VEC2')
    joint=acc(joints,'4H',5123,'VEC4');weight=acc(weights,'4f',5126,'VEC4');idx=acc(indices,'I',5125,'SCALAR');mo=acc(morph,'3f',5126,'VEC3')
    matrices=[]
    for x,y,z in glob:matrices.append((1,0,0,0,0,1,0,0,0,0,1,0,-x,-y,-z,1))
    ibm=acc(matrices,'16f',5126,'MAT4')
    def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,2,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(bytes([0,220,170,100,160,210,240,0,160,210,240,220,170,100])))+chunk(b'IEND',b'')
    imageview=raw(png)
    doc={'asset':{'version':'2.0','generator':'Hyprwalk Stage3B self-authored test fixture'},'scene':0,'scenes':[{'nodes':[0,1]}],
         'nodes':nodes,'skins':[{'joints':list(range(1,len(nodes))),'skeleton':1,'inverseBindMatrices':ibm}],
         'meshes':[{'name':'rig','primitives':[{'attributes':{'POSITION':pos,'NORMAL':nor,'TEXCOORD_0':tex,'JOINTS_0':joint,'WEIGHTS_0':weight},'indices':idx,'material':0,'targets':[{'POSITION':mo}]}],'weights':[0],'extras':{'targetNames':['Smile']}}],
         'materials':[{'name':'cloth','pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1},'doubleSided':True}],
         'textures':[{'source':0}],'images':[{'bufferView':imageview,'mimeType':'image/png'}]}
    if clips:
        time=acc([(0,),(.5,),(1,)],'f',5126,'SCALAR');anims=[]
        for i,name in enumerate(['Idle','Walk','Run','Jump','Fall']):
            out=acc([(0,.95,0),(0,.95+(i+1)*.06,0),(0,.95,0)],'3f',5126,'VEC3')
            anims.append({'name':name,'samplers':[{'input':time,'output':out,'interpolation':'LINEAR'}],'channels':[{'sampler':0,'target':{'node':1,'path':'translation'}}]})
        doc['animations']=anims
    while len(blob)%4:blob.append(0)
    doc.update(buffers=[{'byteLength':len(blob)}],bufferViews=views,accessors=accessors)
    js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*((-len(js))%4)
    result=struct.pack('<III',0x46546c67,2,12+8+len(js)+8+len(blob))+struct.pack('<II',len(js),0x4e4f534a)+js+struct.pack('<II',len(blob),0x004e4942)+blob
    path.write_bytes(result)
if __name__=='__main__':
    out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generate(out/'humanoid.glb');generate(out/'clips.glb',True)
    print('Generated humanoid.glb and clips.glb (self-authored, embedded skinned fixtures).')
