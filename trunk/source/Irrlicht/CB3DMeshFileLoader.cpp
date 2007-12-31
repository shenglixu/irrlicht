
// B3D Mesh loader
//file format designed by mark sibly for the blitz3d engine and has been declared public domain
//loader by luke hoschke

#include "IrrCompileConfig.h"
#ifdef _IRR_COMPILE_WITH_B3D_LOADER_

#include "CB3DMeshFileLoader.h"

#include "IVideoDriver.h"
#include "os.h"


namespace irr
{
namespace scene
{

//! Constructor
CB3DMeshFileLoader::CB3DMeshFileLoader(scene::ISceneManager* smgr)
: NormalsInFile(false), SceneManager(smgr), AnimatedMesh(0), B3DFile(0)
{
	#ifdef _DEBUG
	setDebugName("CB3DMeshFileLoader");
	#endif
}


//! destructor
CB3DMeshFileLoader::~CB3DMeshFileLoader()
{
	for (s32 n=Materials.size()-1; n>=0; --n)
		delete Materials[n].Material;
}


//! returns true if the file maybe is able to be loaded by this class
//! based on the file extension (e.g. ".bsp")
bool CB3DMeshFileLoader::isALoadableFileExtension(const c8* fileName) const
{
	return strstr(fileName, ".b3d") != 0;
}


//! creates/loads an animated mesh from the file.
//! \return Pointer to the created mesh. Returns 0 if loading failed.
//! If you no longer need the mesh, you should call IAnimatedMesh::drop().
//! See IReferenceCounted::drop() for more information.
IAnimatedMesh* CB3DMeshFileLoader::createMesh(io::IReadFile* f)
{
	if (!f)
		return 0;

	B3DFile = f;
	AnimatedMesh = new scene::CSkinnedMesh();

	Buffers = &AnimatedMesh->getMeshBuffers();
	AllJoints = &AnimatedMesh->getAllJoints();

	if ( load() )
	{
		AnimatedMesh->finalize();
	}
	else
	{
		AnimatedMesh->drop();
		AnimatedMesh = 0;
	}

	return AnimatedMesh;
}


bool CB3DMeshFileLoader::load()
{
	B3dStack.clear();

	NormalsInFile=false;

	//------ Get header ------

	SB3dChunkHeader header;
	B3DFile->read(&header, sizeof(header));
#ifdef __BIG_ENDIAN__
	header.size = os::Byteswap::byteswap(header.size);
#endif

	if ( strncmp( header.name, "BB3D", 4 ) != 0 )
	{
		os::Printer::log("File is not a b3d file. Loading failed (No header found)", B3DFile->getFileName(), ELL_ERROR);
		return false;
	}

	// Add main chunk...
	B3dStack.push_back(SB3dChunk(header, B3DFile->getPos()-8));

	// Get file version, but ignore it, as it's not important with b3d files...
	s32 fileVersion;
	B3DFile->read(&fileVersion, sizeof(fileVersion));
#ifdef __BIG_ENDIAN__
	fileVersion = os::Byteswap::byteswap(fileVersion);
#endif

	//------ Read main chunk ------

	while ( (B3dStack.getLast().startposition + B3dStack.getLast().length) > B3DFile->getPos() )
	{
		B3DFile->read(&header, sizeof(header));
#ifdef __BIG_ENDIAN__
		header.size = os::Byteswap::byteswap(header.size);
#endif
		B3dStack.push_back(SB3dChunk(header, B3DFile->getPos()-8));

		if ( strncmp( B3dStack.getLast().name, "TEXS", 4 ) == 0 )
		{
			if (!readChunkTEXS())
				return false;
		}
		else if ( strncmp( B3dStack.getLast().name, "BRUS", 4 ) == 0 )
		{
			if (!readChunkBRUS())
				return false;
		}
		else if ( strncmp( B3dStack.getLast().name, "NODE", 4 ) == 0 )
		{
			if (!readChunkNODE((CSkinnedMesh::SJoint*)0) )
				return false;
		}
		else
		{
			os::Printer::log("Unknown chunk found in mesh base - skipping");
			B3DFile->seek(B3dStack.getLast().startposition + B3dStack.getLast().length);
			B3dStack.erase(B3dStack.size()-1);
		}
	}

	B3dStack.clear();

	BaseVertices.clear();
	AnimatedVertices_VertexID.clear();
	AnimatedVertices_BufferID.clear();

	Materials.clear();
	Textures.clear();

	Buffers=0;
	AllJoints=0;

	return true;
}


bool CB3DMeshFileLoader::readChunkNODE(CSkinnedMesh::SJoint *InJoint)
{
	const core::stringc JointName = readString();

	f32 position[3], scale[3], rotation[4];

	readFloats(position, 3);
	readFloats(scale, 3);
	readFloats(rotation, 4);

	CSkinnedMesh::SJoint *Joint = AnimatedMesh->createJoint(InJoint);

	Joint->Name = JointName;
	Joint->Animatedposition = core::vector3df(position[0],position[1],position[2]) ;
	Joint->Animatedscale = core::vector3df(scale[0],scale[1],scale[2]);
	Joint->Animatedrotation = core::quaternion(rotation[1], rotation[2], rotation[3], rotation[0]);

	//Build LocalMatrix:

	core::matrix4 positionMatrix;
	positionMatrix.setTranslation( Joint->Animatedposition );
	core::matrix4 scaleMatrix;
	scaleMatrix.setScale( Joint->Animatedscale );
	core::matrix4 rotationMatrix = Joint->Animatedrotation.getMatrix();

	Joint->LocalMatrix = positionMatrix * rotationMatrix * scaleMatrix;

	if (InJoint)
		Joint->GlobalMatrix = InJoint->GlobalMatrix * Joint->LocalMatrix;
	else
		Joint->GlobalMatrix = Joint->LocalMatrix;

	while(B3dStack.getLast().startposition + B3dStack.getLast().length > B3DFile->getPos()) // this chunk repeats
	{
		SB3dChunkHeader header;
		B3DFile->read(&header, sizeof(header));
#ifdef __BIG_ENDIAN__
		header.size = os::Byteswap::byteswap(header.size);
#endif

		B3dStack.push_back(SB3dChunk(header, B3DFile->getPos()-8));

		if ( strncmp( B3dStack.getLast().name, "NODE", 4 ) == 0 )
		{
			if (!readChunkNODE(Joint))
				return false;
		}
		else if ( strncmp( B3dStack.getLast().name, "MESH", 4 ) == 0 )
		{
			if (!readChunkMESH(Joint))
				return false;
		}
		else if ( strncmp( B3dStack.getLast().name, "BONE", 4 ) == 0 )
		{
			if (!readChunkBONE(Joint))
				return false;
		}
		else if ( strncmp( B3dStack.getLast().name, "KEYS", 4 ) == 0 )
		{
			if(!readChunkKEYS(Joint))
				return false;
		}
		else if ( strncmp( B3dStack.getLast().name, "ANIM", 4 ) == 0 )
		{
			if (!readChunkANIM())
				return false;
		}
		else
		{
			os::Printer::log("Unknown chunk found in node chunk - skipping");
			B3DFile->seek(B3dStack.getLast().startposition + B3dStack.getLast().length);
			B3dStack.erase(B3dStack.size()-1);
		}
	}

	B3dStack.erase(B3dStack.size()-1);

	return true;
}


bool CB3DMeshFileLoader::readChunkMESH(CSkinnedMesh::SJoint *InJoint)
{
	const s32 vertices_Start=BaseVertices.size(); //B3Ds have Vertex ID's local within the mesh I don't want this

	s32 brush_id;
	B3DFile->read(&brush_id, sizeof(brush_id));
#ifdef __BIG_ENDIAN__
	brush_id = os::Byteswap::byteswap(brush_id);
#endif

	NormalsInFile=false;

	while((B3dStack.getLast().startposition + B3dStack.getLast().length) > B3DFile->getPos()) //this chunk repeats
	{
		SB3dChunkHeader header;
		B3DFile->read(&header, sizeof(header));
#ifdef __BIG_ENDIAN__
		header.size = os::Byteswap::byteswap(header.size);
#endif

		B3dStack.push_back(SB3dChunk(header, B3DFile->getPos()-8));

		if ( strncmp( B3dStack.getLast().name, "VRTS", 4 ) == 0 )
		{
			if (!readChunkVRTS(InJoint))
				return false;
		}
		else if ( strncmp( B3dStack.getLast().name, "TRIS", 4 ) == 0 )
		{
			scene::SSkinMeshBuffer *MeshBuffer = AnimatedMesh->createBuffer();

			if (brush_id!=-1)
				MeshBuffer->Material=(*Materials[brush_id].Material);

			if(readChunkTRIS(MeshBuffer,AnimatedMesh->getMeshBuffers().size()-1, vertices_Start)==false)
				return false;

			if (!NormalsInFile)
			{
				s32 i;

				for ( i=0; i<(s32)MeshBuffer->Indices.size(); i+=3)
				{
					core::plane3df p(MeshBuffer->getVertex(MeshBuffer->Indices[i+0])->Pos,
							MeshBuffer->getVertex(MeshBuffer->Indices[i+1])->Pos,
							MeshBuffer->getVertex(MeshBuffer->Indices[i+2])->Pos);

					MeshBuffer->getVertex(MeshBuffer->Indices[i+0])->Normal += p.Normal;
					MeshBuffer->getVertex(MeshBuffer->Indices[i+1])->Normal += p.Normal;
					MeshBuffer->getVertex(MeshBuffer->Indices[i+2])->Normal += p.Normal;
				}

				for ( i = 0; i<(s32)MeshBuffer->getVertexCount(); ++i )
				{
					MeshBuffer->getVertex(i)->Normal.normalize();
					BaseVertices[vertices_Start+i].Normal=MeshBuffer->getVertex(i)->Normal;
				}
			}
		}
		else
		{
			os::Printer::log("Unknown chunk found in mesh - skipping");
			B3DFile->seek(B3dStack.getLast().startposition + B3dStack.getLast().length);
			B3dStack.erase(B3dStack.size()-1);
		}
	}

	B3dStack.erase(B3dStack.size()-1);

	return true;
}


/*
VRTS:
  int flags                   ;1=normal values present, 2=rgba values present
  int tex_coord_sets          ;texture coords per vertex (eg: 1 for simple U/V) max=8
				but we only support 3
  int tex_coord_set_size      ;components per set (eg: 2 for simple U/V) max=4
  {
  float x,y,z                 ;always present
  float nx,ny,nz              ;vertex normal: present if (flags&1)
  float red,green,blue,alpha  ;vertex color: present if (flags&2)
  float tex_coords[tex_coord_sets][tex_coord_set_size]	;tex coords
  }
*/
bool CB3DMeshFileLoader::readChunkVRTS(CSkinnedMesh::SJoint *InJoint)
{
	const s32 max_tex_coords = 3;
	s32 flags, tex_coord_sets, tex_coord_set_size;

	B3DFile->read(&flags, sizeof(flags));
	B3DFile->read(&tex_coord_sets, sizeof(tex_coord_sets));
	B3DFile->read(&tex_coord_set_size, sizeof(tex_coord_set_size));
#ifdef __BIG_ENDIAN__
	flags = os::Byteswap::byteswap(flags);
	tex_coord_sets = os::Byteswap::byteswap(tex_coord_sets);
	tex_coord_set_size = os::Byteswap::byteswap(tex_coord_set_size);
#endif

	if (tex_coord_sets >= max_tex_coords || tex_coord_set_size >= 4) // Something is wrong
	{
		os::Printer::log("tex_coord_sets or tex_coord_set_size too big", B3DFile->getFileName(), ELL_ERROR);
		return false;
	}

	//------ Allocate Memory, for speed -----------//

	s32 NumberOfReads = 3;

	if (flags & 1)
		NumberOfReads += 3;
	if (flags & 2)
		NumberOfReads += 4;

	NumberOfReads += tex_coord_sets*tex_coord_set_size;

	const s32 MemoryNeeded = (B3dStack.getLast().length / sizeof(f32)) / NumberOfReads;

	BaseVertices.reallocate(MemoryNeeded + BaseVertices.size() + 1);
	AnimatedVertices_VertexID.reallocate(MemoryNeeded + AnimatedVertices_VertexID.size() + 1);

	//--------------------------------------------//

	while( (B3dStack.getLast().startposition + B3dStack.getLast().length) > B3DFile->getPos()) // this chunk repeats
	{
		f32 position[3];
		f32 normal[3]={0.f, 0.f, 0.f};
		f32 color[4]={1.0f, 1.0f, 1.0f, 1.0f};
		f32 tex_coords[max_tex_coords][4];

		readFloats(position, 3);

		if (flags & 1)
		{
			NormalsInFile = true;
			readFloats(normal, 3);
		}

		if (flags & 2)
			readFloats(color, 4);

		for (s32 i=0; i<tex_coord_sets; ++i)
			readFloats(tex_coords[i], tex_coord_set_size);

		f32 tu=0.0f, tv=0.0f;
		if (tex_coord_sets >= 1 && tex_coord_set_size >= 2)
		{
			tu=tex_coords[0][0];
			tv=tex_coords[0][1];
		}

		f32 tu2=0.0f, tv2=0.0f;
		if (tex_coord_sets>1 && tex_coord_set_size>1)
		{
			tu2=tex_coords[1][0];
			tv2=tex_coords[1][1];
		}

		// Create Vertex...
		video::S3DVertex2TCoords Vertex(position[0], position[1], position[2],
				normal[0], normal[1], normal[2],
				video::SColorf(color[0], color[1], color[2], color[3]).toSColor(),
				tu, tv, tu2, tv2);

		// Transform the Vertex position by nested node...
		InJoint->GlobalMatrix.transformVect(Vertex.Pos);
		InJoint->GlobalMatrix.rotateVect(Vertex.Normal);

		//Add it...
		BaseVertices.push_back(Vertex);

		AnimatedVertices_VertexID.push_back(-1);
		AnimatedVertices_BufferID.push_back(-1);
	}

	B3dStack.erase(B3dStack.size()-1);

	return true;
}


bool CB3DMeshFileLoader::readChunkTRIS(scene::SSkinMeshBuffer *MeshBuffer, u32 MeshBufferID, s32 vertices_Start)
{
	bool showVertexWarning=false;

	s32 triangle_brush_id; // Note: Irrlicht can't have different brushes for each triangle (using a workaround)
	B3DFile->read(&triangle_brush_id, sizeof(triangle_brush_id));
#ifdef __BIG_ENDIAN__
	triangle_brush_id = os::Byteswap::byteswap(triangle_brush_id);
#endif

	SB3dMaterial *B3dMaterial;

	if (triangle_brush_id != -1)
		B3dMaterial = &Materials[triangle_brush_id];
	else
		B3dMaterial = 0;

	if (B3dMaterial)
		MeshBuffer->Material = (*B3dMaterial->Material);

	const s32 MemoryNeeded = B3dStack.getLast().length / sizeof(s32);
	MeshBuffer->Indices.reallocate(MemoryNeeded + MeshBuffer->Indices.size() + 1);

	while((B3dStack.getLast().startposition + B3dStack.getLast().length) > B3DFile->getPos()) // this chunk repeats
	{
		s32 vertex_id[3];

		B3DFile->read(vertex_id, 3*sizeof(s32));
#ifdef __BIG_ENDIAN__
		vertex_id[0] = os::Byteswap::byteswap(vertex_id[0]);
		vertex_id[1] = os::Byteswap::byteswap(vertex_id[1]);
		vertex_id[2] = os::Byteswap::byteswap(vertex_id[2]);
#endif

		//Make Ids global:
		vertex_id[0] += vertices_Start;
		vertex_id[1] += vertices_Start;
		vertex_id[2] += vertices_Start;

		for(s32 i=0; i<3; ++i)
		{
			if ((u32)vertex_id[i] >= AnimatedVertices_VertexID.size())
			{
				os::Printer::log("Illegal vertex index found", B3DFile->getFileName(), ELL_ERROR);
				return false;
			}

			if (AnimatedVertices_VertexID[ vertex_id[i] ] != -1)
			{
				if ( AnimatedVertices_BufferID[ vertex_id[i] ] != (s32)MeshBufferID ) //If this vertex is linked in a different meshbuffer
				{
					AnimatedVertices_VertexID[ vertex_id[i] ] = -1;
					AnimatedVertices_BufferID[ vertex_id[i] ] = -1;
					showVertexWarning=true;
				}
			}
			if (AnimatedVertices_VertexID[ vertex_id[i] ] == -1) //If this vertex is not in the meshbuffer
			{
				//Check for lightmapping:
				if (BaseVertices[ vertex_id[i] ].TCoords2 != core::vector2df(0.f,0.f))
					MeshBuffer->MoveTo_2TCoords(); //Will only affect the meshbuffer the first time this is called

				//Add the vertex to the meshbuffer:
				if (MeshBuffer->VertexType == video::EVT_STANDARD)
					MeshBuffer->Vertices_Standard.push_back( BaseVertices[ vertex_id[i] ] );
				else
					MeshBuffer->Vertices_2TCoords.push_back(BaseVertices[ vertex_id[i] ] );

				//create vertex id to meshbuffer index link:
				AnimatedVertices_VertexID[ vertex_id[i] ] = MeshBuffer->getVertexCount()-1;
				AnimatedVertices_BufferID[ vertex_id[i] ] = MeshBufferID;

				if (B3dMaterial)
				{
					// Apply Material/Colour/etc...
					video::S3DVertex *Vertex=MeshBuffer->getVertex(MeshBuffer->getVertexCount()-1);

					if (Vertex->Color.getAlpha() == 255)
						Vertex->Color.setAlpha( (s32)(B3dMaterial->alpha * 255.0f) );

					// Use texture's scale
					if (B3dMaterial->Textures[0])
					{
						Vertex->TCoords.X *= B3dMaterial->Textures[0]->Xscale;
						Vertex->TCoords.Y *= B3dMaterial->Textures[0]->Yscale;
					}
					/*
					if (B3dMaterial->Textures[1])
					{
						Vertex->TCoords2.X *=B3dMaterial->Textures[1]->Xscale;
						Vertex->TCoords2.Y *=B3dMaterial->Textures[1]->Yscale;
					}
					*/
				}
			}
		}

		MeshBuffer->Indices.push_back( AnimatedVertices_VertexID[ vertex_id[0] ] );
		MeshBuffer->Indices.push_back( AnimatedVertices_VertexID[ vertex_id[1] ] );
		MeshBuffer->Indices.push_back( AnimatedVertices_VertexID[ vertex_id[2] ] );
	}

	B3dStack.erase(B3dStack.size()-1);

	if (showVertexWarning)
		os::Printer::log("B3dMeshLoader: Warning, different meshbuffers linking to the same vertex, this will cause problems with animated meshes");

	return true;
}


bool CB3DMeshFileLoader::readChunkBONE(CSkinnedMesh::SJoint *InJoint)
{
	if (B3dStack.getLast().length > 8)
	{
		while((B3dStack.getLast().startposition + B3dStack.getLast().length) > B3DFile->getPos()) // this chunk repeats
		{
			CSkinnedMesh::SWeight *weight=AnimatedMesh->createWeight(InJoint);

			u32 globalVertexID;

			B3DFile->read(&globalVertexID, sizeof(globalVertexID));
			B3DFile->read(&weight->strength, sizeof(weight->strength));
#ifdef __BIG_ENDIAN__
			globalVertexID = os::Byteswap::byteswap(globalVertexID);
			weight->strength = os::Byteswap::byteswap(weight->strength);
#endif

			if (AnimatedVertices_VertexID[globalVertexID]==-1)
			{
				os::Printer::log("B3dMeshLoader: Weight has bad vertex id (no link to meshbuffer index found)");
				weight->vertex_id = weight->buffer_id = 0;
			}
			else
			{
				//Find the MeshBuffer and Vertex index from the Global Vertex ID:
				weight->vertex_id = AnimatedVertices_VertexID[globalVertexID];
				weight->buffer_id = AnimatedVertices_BufferID[globalVertexID];
			}
		}
	}

	B3dStack.erase(B3dStack.size()-1);
	return true;
}


bool CB3DMeshFileLoader::readChunkKEYS(CSkinnedMesh::SJoint *InJoint)
{
	s32 flags;
	B3DFile->read(&flags, sizeof(flags));
#ifdef __BIG_ENDIAN__
	flags = os::Byteswap::byteswap(flags);
#endif

	while((B3dStack.getLast().startposition + B3dStack.getLast().length) > B3DFile->getPos()) //this chunk repeats
	{
		s32 frame;

		B3DFile->read(&frame, sizeof(frame));
		#ifdef __BIG_ENDIAN__
		frame = os::Byteswap::byteswap(frame);
		#endif

		// Add key frames
		if (flags & 1)
		{
			f32 positionData[3];
			readFloats(positionData, 3);
			CSkinnedMesh::SPositionKey *Key=AnimatedMesh->createPositionKey(InJoint);
			Key->frame = (f32)frame;
			Key->position.set(positionData[0], positionData[1], positionData[2]);
		}
		if (flags & 2)
		{
			f32 scaleData[3];
			readFloats(scaleData, 3);
			CSkinnedMesh::SScaleKey *Key=AnimatedMesh->createScaleKey(InJoint);
			Key->frame = (f32)frame;
			Key->scale.set(scaleData[0], scaleData[1], scaleData[2]);
		}
		if (flags & 4)
		{
			f32 rotationData[4];
			readFloats(rotationData, 4);
			CSkinnedMesh::SRotationKey *Key=AnimatedMesh->createRotationKey(InJoint);
			Key->frame = (f32)frame;
			Key->rotation.set(rotationData[1], rotationData[2], rotationData[3], rotationData[0]); // meant to be in this order
		}
	}

	B3dStack.erase(B3dStack.size()-1);
	return true;
}


bool CB3DMeshFileLoader::readChunkANIM()
{
	s32 animFlags; //not stored\used
	s32 animFrames;//not stored\used
	f32 animFPS; //not stored\used

	B3DFile->read(&animFlags, sizeof(s32));
	B3DFile->read(&animFrames, sizeof(s32));
	readFloats(&animFPS, 1);

	#ifdef __BIG_ENDIAN__
		animFlags = os::Byteswap::byteswap(animFlags);
		animFrames = os::Byteswap::byteswap(animFrames);
	#endif

	B3dStack.erase(B3dStack.size()-1);
	return true;
}


bool CB3DMeshFileLoader::readChunkTEXS()
{
	bool Previous32BitTextureFlag = SceneManager->getVideoDriver()->getTextureCreationFlag(video::ETCF_ALWAYS_32_BIT);
	SceneManager->getVideoDriver()->setTextureCreationFlag(video::ETCF_ALWAYS_32_BIT, true);

	while((B3dStack.getLast().startposition + B3dStack.getLast().length) > B3DFile->getPos()) //this chunk repeats
	{
		core::stringc TextureName=readString();

		TextureName=stripPathFromString(B3DFile->getFileName(),true) + stripPathFromString(TextureName,false);

		SB3dTexture B3dTexture;
		B3dTexture.Texture=SceneManager->getVideoDriver()->getTexture ( TextureName.c_str() );

		B3DFile->read(&B3dTexture.Flags, sizeof(s32));
		B3DFile->read(&B3dTexture.Blend, sizeof(s32));
#ifdef __BIG_ENDIAN__
		B3dTexture.Flags = os::Byteswap::byteswap(B3dTexture.Flags);
		B3dTexture.Blend = os::Byteswap::byteswap(B3dTexture.Blend);
#endif
		readFloats(&B3dTexture.Xpos, 1);
		readFloats(&B3dTexture.Ypos, 1);
		readFloats(&B3dTexture.Xscale, 1);
		readFloats(&B3dTexture.Yscale, 1);
		readFloats(&B3dTexture.Angle, 1);

		Textures.push_back(B3dTexture);
	}

	B3dStack.erase(B3dStack.size()-1);

	SceneManager->getVideoDriver()->setTextureCreationFlag(video::ETCF_ALWAYS_32_BIT, Previous32BitTextureFlag);

	return true;
}


bool CB3DMeshFileLoader::readChunkBRUS()
{
	u32 n_texs;
	B3DFile->read(&n_texs, sizeof(u32));
#ifdef __BIG_ENDIAN__
	n_texs = os::Byteswap::byteswap(n_texs);
#endif

	if (n_texs>video::MATERIAL_MAX_TEXTURES)
	{
		os::Printer::log("Too many textures used in one material", B3DFile->getFileName(), ELL_WARNING);
	}
	// number of texture ids read for Irrlicht
	const u32 num_textures = core::min_(n_texs, video::MATERIAL_MAX_TEXTURES);
	// number of bytes to skip (for ignored texture ids)
	const u32 n_texs_offset = (num_textures<n_texs)?(n_texs-num_textures)*sizeof(s32):0;

	while((B3dStack.getLast().startposition + B3dStack.getLast().length) > B3DFile->getPos()) //this chunk repeats
	{
		// This is what blitz basic calls a brush, like a Irrlicht Material

		readString(); //MaterialName not used, but still need to read it

		SB3dMaterial B3dMaterial;

		B3dMaterial.Material = new video::SMaterial();

		readFloats(&B3dMaterial.red, 1);
		readFloats(&B3dMaterial.green, 1);
		readFloats(&B3dMaterial.blue, 1);
		readFloats(&B3dMaterial.alpha, 1);
		readFloats(&B3dMaterial.shininess, 1);

		B3DFile->read(&B3dMaterial.blend, sizeof(B3dMaterial.blend));
		B3DFile->read(&B3dMaterial.fx, sizeof(B3dMaterial.fx));
#ifdef __BIG_ENDIAN__
		B3dMaterial.blend = os::Byteswap::byteswap(B3dMaterial.blend);
		B3dMaterial.fx = os::Byteswap::byteswap(B3dMaterial.fx);
#endif

		for (u32 i=0; i<num_textures; ++i)
		{
			s32 texture_id=-1;
			B3DFile->read(&texture_id, sizeof(s32));
#ifdef __BIG_ENDIAN__
			texture_id = os::Byteswap::byteswap(texture_id);
#endif
			//--- Get pointers to the texture, based on the IDs ---
			if (texture_id != -1)
				B3dMaterial.Textures[i]=&Textures[texture_id];
			else
				B3dMaterial.Textures[i]=0;
		}
		// skip other texture ids
		if (n_texs_offset)
			B3DFile->seek(n_texs_offset, true);

		//Fixes problems when the lightmap is on the first texture:
		if (B3dMaterial.Textures[0] != 0)
		{
			if (B3dMaterial.Textures[0]->Flags & 65536) // 65536 = secondary UV
			{
				SB3dTexture *TmpTexture;
				TmpTexture = B3dMaterial.Textures[1];
				B3dMaterial.Textures[1] = B3dMaterial.Textures[0];
				B3dMaterial.Textures[0] = TmpTexture;
			}
		}

		//If the first texture is empty:
		if (B3dMaterial.Textures[1] != 0 && B3dMaterial.Textures[0] == 0)
		{
			B3dMaterial.Textures[0] = B3dMaterial.Textures[1];
			B3dMaterial.Textures[1] = 0;
		}

		if (B3dMaterial.Textures[0] != 0)
			B3dMaterial.Material->setTexture(0, B3dMaterial.Textures[0]->Texture);
		if (B3dMaterial.Textures[1] != 0)
			B3dMaterial.Material->setTexture(1, B3dMaterial.Textures[1]->Texture);

		//------ Convert blitz flags/blend to irrlicht -------

		//Two textures:
		if (B3dMaterial.Textures[1])
		{
			if (B3dMaterial.alpha==1.f)
			{
				if (B3dMaterial.Textures[1]->Blend & 5) //(Multiply 2)
					B3dMaterial.Material->MaterialType = video::EMT_LIGHTMAP_M2;
				else
					B3dMaterial.Material->MaterialType = video::EMT_LIGHTMAP;
			}
			else
				B3dMaterial.Material->MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
		}
		else if (B3dMaterial.Textures[0]) //One texture:
		{
			if (B3dMaterial.Textures[0]->Flags & 2) //(Alpha mapped)
				B3dMaterial.Material->MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
			else if (B3dMaterial.Textures[0]->Flags & 4) //(Masked)
				B3dMaterial.Material->MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF; // Todo: create color key texture
			else if (B3dMaterial.alpha == 1.f)
				B3dMaterial.Material->MaterialType = video::EMT_SOLID;
			else
				B3dMaterial.Material->MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
		}
		else //No texture:
		{
			if (B3dMaterial.alpha == 1.f)
				B3dMaterial.Material->MaterialType = video::EMT_SOLID;
			else
				B3dMaterial.Material->MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
		}

		B3dMaterial.Material->AmbientColor = video::SColorf(B3dMaterial.red, B3dMaterial.green, B3dMaterial.blue, B3dMaterial.alpha).toSColor ();

		//------ Material fx ------

		if (B3dMaterial.fx & 1) //full-bright
		{
			B3dMaterial.Material->AmbientColor = video::SColor(255, 255, 255, 255);
			B3dMaterial.Material->Lighting = false;
		}

		//if (B3dMaterial.fx & 2) //use vertex colors instead of brush color

		if (B3dMaterial.fx & 4) //flatshaded
			B3dMaterial.Material->GouraudShading = false;

		if (B3dMaterial.fx & 16) //disable backface culling
			B3dMaterial.Material->BackfaceCulling = false;

		if (B3dMaterial.fx & 32) //force vertex alpha-blending
			B3dMaterial.Material->MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;

		B3dMaterial.Material->DiffuseColor = video::SColorf(B3dMaterial.red, B3dMaterial.green, B3dMaterial.blue, B3dMaterial.alpha).toSColor ();
		B3dMaterial.Material->EmissiveColor = video::SColorf(0.5, 0.5, 0.5, 0).toSColor ();
		B3dMaterial.Material->SpecularColor = video::SColorf(0, 0, 0, 0).toSColor ();
		B3dMaterial.Material->Shininess = B3dMaterial.shininess;

		Materials.push_back(B3dMaterial);
	}

	B3dStack.erase(B3dStack.size()-1);

	return true;
}


core::stringc CB3DMeshFileLoader::readString()
{
	core::stringc newstring;
	while (B3DFile->getPos() <= B3DFile->getSize())
	{
		c8 character;
		B3DFile->read(&character, sizeof(character));
		if (character==0)
			break;
		newstring.append(character);
	}
	return newstring;
}


core::stringc CB3DMeshFileLoader::stripPathFromString(const core::stringc& string, bool returnPath) const
{
	s32 slashIndex=string.findLast('/'); // forward slash
	s32 backSlash=string.findLast('\\'); // back slash

	if (backSlash>slashIndex) slashIndex=backSlash;

	if (slashIndex==-1)//no slashes found
	{
		if (returnPath)
			return core::stringc(); //no path to return
		else
			return string;
	}

	if (returnPath)
		return string.subString(0, slashIndex + 1);
	else
		return string.subString(slashIndex+1, string.size() - (slashIndex+1));
}

void CB3DMeshFileLoader::readFloats(f32* vec, u32 count)
{
	B3DFile->read(vec, count*sizeof(f32));
	#ifdef __BIG_ENDIAN__
	for (u32 n=0; n<count; ++n)
		vec[n] = os::Byteswap::byteswap(vec[n]);
	#endif
}

} // end namespace scene
} // end namespace irr


#endif // _IRR_COMPILE_WITH_B3D_LOADER_

