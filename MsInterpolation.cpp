#include <OpenMesh/Core/IO/MeshIO.hh>
#include <OpenMesh/Tools/Utils/getopt.h>
#include <iostream>
#include <string>
#include <ctime>
#include <cstdlib>
#include <map>
#include <algorithm>
#include <set>
#include <fstream>
#include "MsInterpolation.h"
#include "LeafNodeInterpolation.h"
#include "MultiRegistration.h"


MsInterpolation::MsInterpolation(void)
{
	face_root = NULL;

	min_size_node = 0x7fffffff;
}


MsInterpolation::~MsInterpolation(void)
{
	release_facenode_help(face_root);
}


void MsInterpolation::read_mesh_data( std::string filename, std::string target_file)
{
	bool ret;
	
	ret = IO::read_mesh( this->m_mesh, filename);
	if( ret == false )
	{
		printf("Reading Mesh Data Error in %s", __FUNCTION__);
		exit( -1 );
	}

	std::cout<<"Source Mesh Data:\n";
	std::cout << "# Vertices: " << m_mesh.n_vertices() << std::endl;
	std::cout << "# Edges   : " << m_mesh.n_edges() << std::endl;
	std::cout << "# Faces   : " << m_mesh.n_faces() << std::endl;

	ret = IO::read_mesh( target_mesh, target_file );
	if( ret == false )
	{
		printf("Reading Mesh Data Error in %s", __FUNCTION__);
		exit( -1 );
	}

	std::cout<<"Target Mesh Data:\n";
	std::cout << "# Vertices: " << m_mesh.n_vertices() << std::endl;
	std::cout << "# Edges   : " << m_mesh.n_edges() << std::endl;
	std::cout << "# Faces   : " << m_mesh.n_faces() << std::endl;
}


void MsInterpolation::write_mesh_data( std::string output )
{
	bool ret;
	IO::Options wopt;

	ret = OpenMesh::IO::write_mesh( this->m_mesh, output, wopt);

	if( ret == false )
	{
		printf("Writing Mesh Data Error In %s\n", __FUNCTION__);
	}
	else
	{
		printf("Writing Mesh Data Success!\n");
	}

}



void MsInterpolation::test()
{
	while( 1 )
	{
		release_facenode_help(face_root);
		build_hierarchy_on_face();

		printf("��С�Ľڵ�ĵ����ǣ�%d\n", min_size_node);

		int min = 0x7fffffff, max = 0;
		max_min_height(face_root, min, max);
		printf("Face min:%d, max:%d\n", min, max);

        if( max < 15)
		{
			break;
		}
	}

	build_registration_pair();

	std::cout<<"Build Pair Done!\n";

	build_interpolation(face_root, m_mesh, target_mesh, 0.75);
		
	MyMesh::VertexIter v_iter, v_end( m_mesh.vertices_end());

	for (v_iter = m_mesh.vertices_begin(); v_iter != v_end; ++v_iter)
	{
		MyVector3f v = face_root->pts[ v_iter.handle().idx() ];
		m_mesh.set_point( v_iter, MyMesh::Point( v[0], v[1], v[2]) );
	}

	write_mesh_data( "interpolation_result.off");
}


void MsInterpolation::random_seed_point_helper( int size, int *idx )
{
	srand( time(NULL) );

	int curr_idx = 0;
	bool ret;

	while ( 1 )
	{
		int random_value = rand() % size;

		ret = false;
		for( int i = 0; i != curr_idx; i++)
		{
			if( random_value == idx[ i ] )
			{
				ret = true;
				break;
			}
		}

		if( ! ret )
		{
			idx[ curr_idx++ ] = random_value;
		}

		if( curr_idx == NR_MIN_PATCH_PER_LEVEL )
		{
			break;
		}
	}
}


void MsInterpolation::build_hierarchy_on_face()
{
	if( face_root == NULL)
	{
		face_root = new FaceNode();
	}else
	{
		printf("face root�Ѿ���������!\n");
		return ;
	}

	MyMesh::FaceIter f_iter, f_end( m_mesh.faces_end() );
	MyFaceHandle fh;

	for(f_iter = m_mesh.faces_begin(); f_iter != f_end; ++f_iter)
	{
		fh = f_iter.handle();

		{
			face_root->idx.push_back( fh.idx() );
		}
	}

	build_hierarchy_based_on_face(face_root, 0);
}


void MsInterpolation::build_hierarchy_based_on_face( FaceNode *subroot, int level /*= 0*/ )
{
	FaceNode *curr_node;
	MyFaceHandle curr_face_handle;

	int nr_faces = subroot->idx.size();
	int last_pos[NR_MIN_PATCH_PER_LEVEL];
	
	std::vector<int> face_flag;
	face_flag.assign(nr_faces, -1);

	std::vector<bool> need_iter;
	need_iter.assign(nr_faces, true);

	std::map<int, int> ridx;
	for(int i = 0; i < nr_faces; i++)
	{
		ridx.insert( std::pair<int, int>(subroot->idx[i], i) );
	}

	int seed_idx[NR_MIN_PATCH_PER_LEVEL];
	random_seed_point_helper(nr_faces, seed_idx);

	for(int i = 0; i < NR_MIN_PATCH_PER_LEVEL; i++)
	{
		int selected_idx = seed_idx[ i ];
		curr_node = subroot->next[i] = new FaceNode();

		curr_node->idx.push_back( subroot->idx[ selected_idx ] );
		face_flag[ selected_idx ] = i;

		last_pos[ i ] = 0;//�������
	}

	MyMesh::FaceIter face_iter;
	MyMesh::FaceFaceIter faceface_iter;
	MyFaceHandle close_face_handle;

	bool done;

	int start_pos, end_pos;// ����iterator����

	while( 1 )
	{
		done = true;
		for(int i = 0; i < nr_faces; i++)
		{
			if( face_flag[ i ] == -1 )
			{
				done = false;
				break;
			}
		}

		if( done )
		{
			break;// ��level����
		}

		for(int i = 0; i < NR_MIN_PATCH_PER_LEVEL; i++)
		{

			curr_node = subroot->next[ i ];

			start_pos = last_pos[ i ];
			end_pos = curr_node->idx.size();

			for(int j = start_pos; j != end_pos; j++)
			{
				curr_face_handle = MyFaceHandle( curr_node->idx[ j ] );

				if( ! need_iter[ ridx[ curr_face_handle.idx() ] ] )
				{
					continue;
				}

				for( faceface_iter = m_mesh.ff_begin( curr_face_handle ); faceface_iter; ++faceface_iter )
				{
					close_face_handle = faceface_iter.handle();
					int close_idx = close_face_handle.idx();

					int ret = binary_search( subroot->idx.begin(), subroot->idx.end(), close_idx );
					if ( ret == false )
					{
						continue;// ��face������һ���patch��
					}

					int used = face_flag[ ridx[ close_idx ] ];
					if( used == -1 )
					{
						// ��δ��ʹ��
						curr_node->idx.push_back( close_idx );
						face_flag[ ridx[ close_idx ] ] = i;
					}else
					{
						if( used != i)
						{
							// �ڽ�patch��������
							curr_node->boundray.push_back( close_idx );


						}
					}

				}
				
			}
			last_pos[ i ] = end_pos;

		}

	}


    /*** ���ӱ�Ե ***/
	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{

		FaceNode *node_i = subroot->next[i];
		node_i->idx.insert( node_i->idx.end(), node_i->boundray.begin(), node_i->boundray.end() );
	}


	/*** ȥ�� ***/
	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{

		FaceNode *node_i = subroot->next[i];

		sort( node_i->idx.begin(), node_i->idx.end() ); // ����

		std::vector<int>::iterator iter;

		iter = unique(node_i->idx.begin(), node_i->idx.end() );
		node_i->idx.erase( iter, node_i->idx.end() );
	}

	level++;
	
	for(int i = 0; i < NR_MIN_PATCH_PER_LEVEL; i++)
	{
		int next_idx_len = subroot->next[ i ]->idx.size();
		
		if( next_idx_len < min_size_node )
		{
			min_size_node = next_idx_len;
		}

		if( next_idx_len > LEAF_NODE_MIN_NR )
		{
			build_hierarchy_based_on_face( subroot->next[ i ], level );
		}
	}
}


void MsInterpolation::release_facenode_help( FaceNode * &subroot )
{
	if( subroot == NULL )
	{
		return;
	}

	for(int i = 0; i < NR_MIN_PATCH_PER_LEVEL; i++)
	{
		release_facenode_help( subroot->next[ i ] );
	}

	delete subroot;
	subroot = NULL;
}

void MsInterpolation::max_min_height( FaceNode *subroot, int &min, int &max )
{
	if( subroot == NULL)
	{
		min = 0;
		max = 0;
		return ;
	}

	int mins[NR_MIN_PATCH_PER_LEVEL];
	int maxs[NR_MIN_PATCH_PER_LEVEL];

	for(int i = 0; i < NR_MIN_PATCH_PER_LEVEL; i++)
	{	
		mins[i] = 0x7fffffff;
		maxs[i] = 0;
		max_min_height(subroot->next[i], mins[i], maxs[i]);
	}

	for(int i = 0; i < NR_MIN_PATCH_PER_LEVEL; i++)
	{
		if( mins[i] < min )
		{
			min = mins[i];
		}

		if( maxs[i] > max )
		{
			max = maxs[i];
		}
	}

	min += 1;
	max += 1;
}


void MsInterpolation::build_registration_pair( FaceNode *subroot, int level  )
{

	if( subroot == NULL )
	{
		return;
	}

	bool leaf_node = true;
	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{
		if( subroot->next[i] )
		{
			leaf_node = false;
			break;
		}
	}

	if( leaf_node )
	{
		return;
	}

	/*** ����ýڵ�ĵ�index  ***/
	std::set<int> set_idx[NR_MIN_PATCH_PER_LEVEL];
	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{	
		FaceNode *node_i = subroot->next[i];

		MyMesh::FaceVertexIter fv_iter;
		MyFaceHandle fh;

		int size = node_i->idx.size();
		for (int k = 0; k != size; k++)
		{
			fh = MyFaceHandle( node_i->idx[k] );
			for (fv_iter = m_mesh.fv_begin(fh); fv_iter; ++fv_iter)
			{
				set_idx[i].insert( fv_iter.handle().idx() );
			}
		}


		node_i->pts_index.resize( set_idx[i].size() );
		std::copy( set_idx[i].begin(), set_idx[i].end(), node_i->pts_index.begin() );

		for (int j = 0;  j != set_idx[i].size(); j++)
		{
			node_i->r_idx.insert( std::pair<int, int>(node_i->pts_index[j], j) );
		}

	}

	/***���㽻��***/
	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{
		for (int j = i + 1; j != NR_MIN_PATCH_PER_LEVEL; j++)
		{
			std::vector<int> ret;
			std::vector<int>::iterator iter;

			ret.resize( set_idx[i].size() );

			iter = std::set_intersection(set_idx[i].begin(), set_idx[i].end(), set_idx[j].begin(), set_idx[j].end(), ret.begin() );

			ret.resize( iter - ret.begin() );

			if( ret.empty() )
			{
				continue;
			}


			Pair p = Pair(i, j);

			subroot->P.push_back( p );
			subroot->pl.push_back( ret );
		}

	}

	level++;

	for (int i = 0; i !=  NR_MIN_PATCH_PER_LEVEL; i++)
	{
		build_registration_pair(subroot->next[i], level);
	}

}

void MsInterpolation::build_registration_pair()
{
	MyMesh::VertexIter iter, v_end( m_mesh.vertices_end() );

	face_root->pts_index.reserve( m_mesh.n_vertices() );

	int k = 0;
	for (iter = m_mesh.vertices_begin(); iter != v_end; ++iter)
	{
		face_root->pts_index.push_back( iter.handle().idx());
		face_root->r_idx.insert( std::pair<int, int>(iter.handle().idx(), k++) );
	}

	build_registration_pair( face_root );
}


void MsInterpolation::build_interpolation( FaceNode *subroot, MyMesh &src_mesh, MyMesh &target_mesh, double t )
{
	bool leaf_node = true;
 
	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{
		if( subroot->next[i] != NULL)
		{
			leaf_node = false;
			break;
		}
	}

	/***�����Ҷ�ڵ�***/
	if( leaf_node )
	{
		leaf_node_interpolation(t, subroot, src_mesh,target_mesh);
		return;
	}

	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{
		build_interpolation(subroot->next[i], src_mesh, target_mesh, t);
	}

	/***������׼***/
	int M = NR_MIN_PATCH_PER_LEVEL;
	int P = subroot->P.size();

	std::vector<PointList> pl;

	for (int i = 0; i != P; i++)
	{
		int a = subroot->P[i].a;
		int b = subroot->P[i].b;

		FaceNode *node_a = subroot->next[a];
		FaceNode *node_b = subroot->next[b];
		
		std::vector<int> *curr_p = &subroot->pl[i];
		int size = curr_p->size();
		
		PointList pts;

		for (int j = 0; j != size; j++)
		{
			int idx = (*curr_p)[j];//���еĵ�����
			PairVertex pv;

			pv.a = node_a->pts[ node_a->r_idx[idx] ];
			pv.b = node_b->pts[ node_b->r_idx[idx] ];


			pts.push_back( pv );
		}

		pl.push_back( pts );
	}

	MultiRegistration mr(M, P, &subroot->P, &pl);
	mr.init();

	std::vector<MyMatrix3f> R;
	std::vector<MyVector3f> T;

	mr.get_R_and_T(R, T);

	/***ת����ͳһ������ϵ��***/
	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{
		int size = subroot->next[i]->pts.size();

		for (int j = 0; j != size; j++)
		{
			subroot->next[i]->pts[j] = R[i] * subroot->next[i]->pts[j] + T[i];
		}

	}

	/***����ռ�***/
	int size = subroot->pts_index.size();
	subroot->pts.assign(size, MyVector3f(0, 0, 0) );
	std::vector<int> cnt;

	cnt.assign( size, 0);

	for (int i = 0; i != NR_MIN_PATCH_PER_LEVEL; i++)
	{
		FaceNode *node_i = subroot->next[i];
		int pts_size = node_i->pts_index.size();

		for (int j = 0; j != pts_size; j++)
		{
			int idx = node_i->pts_index[j];
			int r_idx = subroot->r_idx[ idx ];

			cnt[r_idx] += 1;

			subroot->pts[ r_idx ] += node_i->pts[j];
		}

	}

	/**** �����ظ��ĵ������ȡƽ��ֵ ***/
	for (int i = 0; i != size; i++)
	{

		subroot->pts[ i ]  /= cnt[i];

	}

}