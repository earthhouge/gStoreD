/*=============================================================================
# Filename: Database.cpp
# Author: Bookug Lobert
# Mail: 1181955272@qq.com
# Last Modified: 2015-10-23 14:22
# Description: originally written by liyouhuan, modified by zengli
=============================================================================*/

#include "Database.h"

using namespace std;

typedef struct {
	vector<int> partialRes;
	set< pair<int,int> > visitedEdgeSet;
	vector< pair<int,int> > extendedEdge;
} PPResult;

Database::Database(string _name)
{
    this->name = _name;
    string store_path = this->name;

    this->signature_binary_file = "signature.binary";
    this->six_tuples_file = "six_tuples";
    this->db_info_file = "db_info_file.dat";

    string kv_store_path = store_path + "/kv_store";
    this->kvstore = new KVstore(kv_store_path);

    string vstree_store_path = store_path + "/vs_store";
    this->vstree = new VSTree(vstree_store_path);

    this->encode_mode = Database::STRING_MODE;
    this->is_active = false;
    this->sub_num = 0;
    this->pre_num = 0;
    this->literal_num = 0;
    this->entity_num = 0;
    this->triples_num = 0;
	
	//this->internal_tag_arr = new char[this->entity_num + 1];
	//this->internal_tag_arr[this->entity_num] = 0;
}

void
Database::release(FILE* fp0)
{
    fprintf(fp0, "begin to delete DB!\n");
    fflush(fp0);
    delete this->vstree;
    fprintf(fp0, "ok to delete vstree!\n");
    fflush(fp0);
    delete this->kvstore;
    fprintf(fp0, "ok to delete kvstore!\n");
    fflush(fp0);
    //fclose(Util::debug_database);
    //Util::debug_database = NULL;	//debug: when multiple databases
    fprintf(fp0, "ok to delete DB!\n");
    fflush(fp0);
}

Database::~Database()
{
    delete this->vstree;
    delete this->kvstore;
	//delete this->internal_tag_arr;
    //fclose(Util::debug_database);
    //Util::debug_database = NULL;	//debug: when multiple databases
}

bool
Database::load()
{
    bool flag = (this->vstree)->loadTree();
    if (!flag)
    {
        cerr << "load tree error. @Database::load()" << endl;
        return false;
    }

    flag = this->loadDBInfoFile();
    if (!flag)
    {
        cerr << "load database info error. @Database::load()" << endl;
        return false;
    }

    (this->kvstore)->open();
	
	stringstream _internal_path;
	_internal_path << this->getStorePath() << "/internal_nodes.dat";
	
	ifstream _internal_input;
	_internal_input.open(_internal_path.str().c_str());
	char* buffer = new char[this->entity_num + 1];
	_internal_input.read(buffer, this->entity_num);
	buffer[this->entity_num] = 0;
	_internal_input.close();
	this->internal_tag_arr = string(buffer);
	delete[] buffer;

    return true;
}

void 
Database::setInternalVertices(const char* _path){
	//add internal node flags to B+ tree
	//Tree* tp = new Tree(_db_path, string("internal_nodes.dat"), "build");
	string buff;
	ifstream infile;

	infile.open(_path);

	if(!infile){
		cout << "import internal vertices failed." << endl;
	}

	char* buffer = new char[this->entity_num + 1];
	memset(buffer, '0', this->entity_num);
	buffer[this->entity_num] = 0;
	//cout << "begin " << this->internal_tag_arr << " " << this->entity_num << endl;
	while(getline(infile, buff)){
		buff.erase(0, buff.find_first_not_of("\r\t\n "));
		buff.erase(buff.find_last_not_of("\r\t\n ") + 1);
		int _entity_id = (this->kvstore)->getIDByEntity(buff);
		buffer[_entity_id] = '1';
		//cout << _entity_id << "========" << buff << endl;
		/*
		stringstream _ss;
		_ss << _sub_id;
		string s = _ss.str();
		tp->insert(s.c_str(), strlen(s.c_str()), NULL, 0);
		*/
	}

	infile.close();
	
	stringstream _internal_path;
	_internal_path << this->getStorePath() << "/internal_nodes.dat";
	cout << this->getStorePath() << "begin to import internal vertices to database " << _internal_path.str() << endl;
	
	FILE * pFile;
	pFile = fopen (_internal_path.str().c_str(), "wb");
	fwrite(buffer, sizeof(char), this->entity_num, pFile);
	fclose(pFile);
	
	cout << this->getStorePath() << "import internal vertices to database " << _internal_path.str() << " done." << endl;
	
	//tp->save();
	//delete tp;
}

bool
Database::unload()
{
    (this->kvstore)->release();
    delete this->vstree;

    return true;
}

string
Database::getName()
{
    return this->name;
}

SPARQLquery
Database::query(const string _query, ResultSet& _result_set, string& _res, int myRank)
{
    long tv_begin = Util::get_cur_time();

	//cout << "begin Parsing" << endl;
	/*
    DBparser _parser;
    SPARQLquery _sparql_q(_query);
    _parser.sparqlParser(_query, _sparql_q);
	*/
	QueryParser _parser;
	QueryTree query_tree;    
    _parser.sparqlParser(_query, query_tree);
	query_tree.getGroupPattern().getVarset();
	
	SPARQLquery _sparql_q(_query);
	//cout << "before get basic query" << endl;
	this->getBasicQuery(_sparql_q, query_tree.getGroupPattern());


    long tv_parse = Util::get_cur_time();
    //cout << "after Parsing, used " << (tv_parse - tv_begin) << endl;
    //cout << "after Parsing..." << endl << _sparql_q.triple_str() << endl;
	
	for (int i = 0; i < query_tree.getProjection().varset.size(); i++)
		_sparql_q.addQueryVar(query_tree.getProjection().varset[i]);
		
    _sparql_q.encodeQuery(this->kvstore);

    //cout << "sparqlSTR:\t" << _sparql_q.to_str() << endl;

    long tv_encode = Util::get_cur_time();
    //cout << "after Encode, used " << (tv_encode - tv_parse) << "ms." << endl;

    _result_set.select_var_num = _sparql_q.getQueryVarNum();

    (this->vstree)->retrieve(_sparql_q, this->internal_tag_arr);

    long tv_retrieve = Util::get_cur_time();
    //cout << "after Retrieve, used " << _result_set.select_var_num << "ms." << endl;

    int var_num = this->join(_sparql_q, myRank, _res);
	
	//BasicQuery* basic_query=&(_sparql_query.getBasicQuery(0));
	
    return _sparql_q;
}

void Database::getBasicQuery(SPARQLquery& _sparql_q, QueryTree::GroupPattern &grouppattern)
{
    for (int i = 0; i < (int)grouppattern.unions.size(); i++)
        for (int j = 0; j < (int)grouppattern.unions[i].grouppattern_vec.size(); j++)
            getBasicQuery(_sparql_q, grouppattern.unions[i].grouppattern_vec[j]);
    for (int i = 0; i < (int)grouppattern.optionals.size(); i++)
        getBasicQuery(_sparql_q, grouppattern.optionals[i].grouppattern);

    int current_optional = 0;
    int first_patternid = 0;

    grouppattern.initPatternBlockid();
    vector<int> basicqueryid((int)grouppattern.patterns.size(), 0);

    for (int i = 0; i < (int)grouppattern.patterns.size(); i++)
    {
        for (int j = first_patternid; j < i; j++)
            if (grouppattern.patterns[i].varset.hasCommonVar(grouppattern.patterns[j].varset))
                grouppattern.mergePatternBlockid(i, j);

        if ((current_optional != (int)grouppattern.optionals.size() && i == grouppattern.optionals[current_optional].lastpattern) || i + 1 == (int)grouppattern.patterns.size())
        {
            for (int j = first_patternid; j <= i; j++)
                if ((int)grouppattern.patterns[j].varset.varset.size() > 0)
                {
                    if (grouppattern.getRootPatternBlockid(j) == j)			//root node
                    {
                        _sparql_q.addBasicQuery();
                        //this->sparql_query_varset.push_back(Varset());

                        for (int k = first_patternid; k <= i; k++)
                            if (grouppattern.getRootPatternBlockid(k) == j)
                            {
                                _sparql_q.addTriple(Triple(
                                                                 grouppattern.patterns[k].subject.value,
                                                                 grouppattern.patterns[k].predicate.value,
                                                                 grouppattern.patterns[k].object.value));

                                basicqueryid[k] = _sparql_q.getBasicQueryNum() - 1;
                                //this->sparql_query_varset[(int)this->sparql_query_varset.size() - 1] = this->sparql_query_varset[(int)this->sparql_query_varset.size() - 1] + grouppattern.patterns[k].varset;
                            }
                    }
                }
                else	basicqueryid[j] = -1;

            for (int j = first_patternid; j <= i; j++)
                grouppattern.pattern_blockid[j] = basicqueryid[j];

            if (current_optional != (int)grouppattern.optionals.size())	current_optional++;
            first_patternid = i + 1;
        }
    }

    for(int i = 0; i < (int)grouppattern.filter_exists_grouppatterns.size(); i++)
        for (int j = 0; j < (int)grouppattern.filter_exists_grouppatterns[i].size(); j++)
            getBasicQuery(_sparql_q, grouppattern.filter_exists_grouppatterns[i][j]);
}

int
Database::queryASK(const string _query, ResultSet& _result_set, string& _res, int myRank)//, string& query_graph)
{
    long tv_begin = Util::get_cur_time();

	//cout << "begin Parsing" << endl;
	/*
    DBparser _parser;
    SPARQLquery _sparql_q(_query);
    _parser.sparqlParser(_query, _sparql_q);
	*/
	QueryParser _parser;
	QueryTree query_tree;    
    _parser.sparqlParser(_query, query_tree);
	query_tree.getGroupPattern().getVarset();
	
	SPARQLquery _sparql_q(_query);
	//cout << "before get basic query" << endl;
	this->getBasicQuery(_sparql_q, query_tree.getGroupPattern());


    long tv_parse = Util::get_cur_time();
    //cout << "after Parsing, used " << (tv_parse - tv_begin) << endl;
    //cout << "after Parsing..." << endl << _sparql_q.triple_str() << endl;
	
	for (int i = 0; i < query_tree.getProjection().varset.size(); i++)
		_sparql_q.addQueryVar(query_tree.getProjection().varset[i]);
		
    _sparql_q.encodeQuery(this->kvstore);

    //cout << "sparqlSTR:\t" << _sparql_q.to_str() << endl;

    long tv_encode = Util::get_cur_time();
    //cout << "after Encode, used " << (tv_encode - tv_parse) << "ms." << endl;

    _result_set.select_var_num = _sparql_q.getQueryVarNum();

    (this->vstree)->retrieve(_sparql_q, this->internal_tag_arr);

    long tv_retrieve = Util::get_cur_time();
    //cout << "after Retrieve, used " << _result_set.select_var_num << "ms." << endl;

    //int var_num = this->join(_sparql_q, myRank, _res);

    int var_num = this->joinASK(_sparql_q, myRank, _res);
	//printf("after joinASK in Client %d\n", myRank);
	
    return var_num;
}

bool
Database::insert(const string& _insert_rdf_file)
{
    bool flag = this->load();

    if (!flag)
    {
        return false;
    }
    cout << "finish loading" << endl;

    long tv_load = Util::get_cur_time();

    ifstream _fin(_insert_rdf_file.c_str());
    if(!_fin) {
        cerr << "fail to open : " << _insert_rdf_file << ".@insert_test" << endl;
        exit(0);
    }

    TripleWithObjType* triple_array = new TripleWithObjType[RDFParser::TRIPLE_NUM_PER_GROUP];
    //parse a file
    RDFParser _parser(_fin);

    int insert_triple_num = 0;
    long long sum_avg_len = 0;
    Database::log("==> while(true)");
    while(true)
    {
        int parse_triple_num = 0;
        _parser.parseFile(triple_array, parse_triple_num);
        //debug
        {
            stringstream _ss;
            _ss << "finish rdfparser" << insert_triple_num << endl;
            Database::log(_ss.str());
            cout << _ss.str() << endl;
        }

        if(parse_triple_num == 0)
        {
            break;
        }


        /* Process the Triple one by one */
        for(int i = 0; i < parse_triple_num; i ++)
        {
            //debug
//            {
//                stringstream _ss;
//                _ss << "insert triple: " << triple_array[i].toString() << " insert_triple_num=" << insert_triple_num << endl;
//                Database::log(_ss.str());
//            }
            sum_avg_len += this->insertTriple(triple_array[i]);
            insert_triple_num ++;

            //debug
//            {
//                if (insert_triple_num % 100 == 0)
//                {
//                    sum_avg_len /= 100;
//                    cout <<"average update len per 100 triple: " << sum_avg_len <<endl;
//                    sum_avg_len = 0;
//                }
//            }
        }
    }

    long tv_insert = Util::get_cur_time();
    cout << "after insert, used " << (tv_insert - tv_load) << "ms." << endl;

    flag = this->vstree->saveTree();
    if (!flag)
    {
        return false;
    }

    this->kvstore->release();
    flag = this->saveDBInfoFile();
    if (!flag)
    {
        return false;
    }

    cout << "insert rdf triples done." << endl;

    return true;
}

bool
Database::remove(const string& _rdf_file)
{
    // to be implemented...
    return true;
}

bool
Database::build(const string& _rdf_file)
{
    long tv_build_begin = Util::get_cur_time();

    string store_path = this->name;
    Util::create_dir(store_path);

    string kv_store_path = store_path + "/kv_store";
    Util::create_dir(kv_store_path);

    string vstree_store_path = store_path + "/vs_store";
    Util::create_dir(vstree_store_path);

    cout << "begin encode RDF from : " << _rdf_file << " ..." << endl;
    // to be switched to new encodeRDF method.
//    this->encodeRDF(_rdf_file);
    this->encodeRDF_new(_rdf_file);
    cout << "finish encode." << endl;
    string _entry_file = this->getSignatureBFile();
    (this->kvstore)->open();

    cout << "begin build VS-Tree on " << _rdf_file << "..." << endl;
    (this->vstree)->buildTree(_entry_file);

    long tv_build_end = Util::get_cur_time();
    cout << "after build, used " << (tv_build_end - tv_build_begin) << "ms." << endl;
    cout << "finish build VS-Tree." << endl;

    return true;
}

/* root Path of this DB + sixTuplesFile */
string
Database::getSixTuplesFile()
{
    return this->getStorePath() + "/" + this->six_tuples_file;
}

/* root Path of this DB + signatureBFile */
string
Database::getSignatureBFile()
{
    return this->getStorePath() + "/" + this->signature_binary_file;
}

/* root Path of this DB + DBInfoFile */
string
Database::getDBInfoFile()
{
    return this->getStorePath() + "/" + this->db_info_file;
}

/* entityID of a string */
int
Database::getIDofEntity(string _s)
{
	return (this->kvstore)->getIDByEntity(_s);
}

/*
 * private methods:
 */

bool
Database::saveDBInfoFile()
{
    FILE* filePtr = fopen(this->getDBInfoFile().c_str(), "wb");

    if (filePtr == NULL)
    {
        cerr << "error, can not create db info file. @Database::saveDBInfoFile"  << endl;
        return false;
    }

    fseek(filePtr, 0, SEEK_SET);

    fwrite(&this->triples_num, sizeof(int), 1, filePtr);
    fwrite(&this->entity_num, sizeof(int), 1, filePtr);
    fwrite(&this->sub_num, sizeof(int), 1, filePtr);
    fwrite(&this->pre_num, sizeof(int), 1, filePtr);
    fwrite(&this->literal_num, sizeof(int), 1, filePtr);
    fwrite(&this->encode_mode, sizeof(int), 1, filePtr);
    fclose(filePtr);

    return true;
}

bool
Database::loadDBInfoFile()
{
    FILE* filePtr = fopen(this->getDBInfoFile().c_str(), "rb");

    if (filePtr == NULL)
    {
        cerr << "error, can not open db info file. @Database::loadDBInfoFile"  << endl;
        return false;
    }

    fseek(filePtr, 0, SEEK_SET);

    fread(&this->triples_num, sizeof(int), 1, filePtr);
    fread(&this->entity_num, sizeof(int), 1, filePtr);
    fread(&this->sub_num, sizeof(int), 1, filePtr);
    fread(&this->pre_num, sizeof(int), 1, filePtr);
    fread(&this->literal_num, sizeof(int), 1, filePtr);
    fread(&this->encode_mode, sizeof(int), 1, filePtr);
    fclose(filePtr);

    return true;
}

string
Database::getStorePath()
{
    return this->name;
}


/* encode relative signature data of the query graph */
void
Database::buildSparqlSignature(SPARQLquery & _sparql_q)
{
    vector<BasicQuery*>& _query_union = _sparql_q.getBasicQueryVec();
    for(unsigned int i_bq = 0; i_bq < _query_union.size(); i_bq ++)
    {
        BasicQuery* _basic_q = _query_union[i_bq];
        _basic_q->encodeBasicQuery(this->kvstore, _sparql_q.getQueryVar());
    }
}



bool
Database::calculateEntityBitSet(int _sub_id, EntityBitSet & _bitset)
{
    int* _polist = NULL;
    int _list_len = 0;
    (this->kvstore)->getpreIDobjIDlistBysubID(_sub_id, _polist, _list_len);
    Triple _triple;
    _triple.subject = (this->kvstore)->getEntityByID(_sub_id);
    for(int i = 0; i < _list_len; i += 2)
    {
        int _pre_id = _polist[i];
        int _obj_id = _polist[i+1];
        _triple.object = (this->kvstore)->getEntityByID(_obj_id);
        if(_triple.object == "")
        {
            _triple.object = (this->kvstore)->getLiteralByID(_obj_id);
        }
        _triple.predicate = (this->kvstore)->getPredicateByID(_pre_id);
        this->encodeTriple2SubEntityBitSet(_bitset, &_triple);
    }
    return true;
}

/* encode Triple into subject SigEntry */
bool
Database::encodeTriple2SubEntityBitSet(EntityBitSet& _bitset, const Triple* _p_triple)
{
    int _pre_id = -1;
    {
        _pre_id = (this->kvstore)->getIDByPredicate(_p_triple->predicate);
        /* checking whether _pre_id is -1 or not will be more reliable */
    }

    Signature::encodePredicate2Entity(_pre_id, _bitset, BasicQuery::EDGE_OUT);
    if(this->encode_mode == Database::ID_MODE)
    {
        /* TBD */
    }
    else if(this->encode_mode == Database::STRING_MODE)
    {
        Signature::encodeStr2Entity( (_p_triple->object ).c_str(), _bitset);
    }

    return true;
}

/* encode Triple into object SigEntry */
bool
Database::encodeTriple2ObjEntityBitSet(EntityBitSet& _bitset, const Triple* _p_triple)
{
    int _pre_id = -1;
    {
        _pre_id = (this->kvstore)->getIDByPredicate(_p_triple->predicate);
        /* checking whether _pre_id is -1 or not will be more reliable */
    }

    Signature::encodePredicate2Entity(_pre_id, _bitset, BasicQuery::EDGE_IN);
    if(this->encode_mode == Database::ID_MODE)
    {
        /* TBD */
    }
    else if(this->encode_mode == Database::STRING_MODE)
    {
        Signature::encodeStr2Entity( (_p_triple->subject ).c_str(), _bitset);
    }

    return true;
}

/* check whether the relative 3-tuples exist
 * usually, through sp2olist */
bool
Database::exist_triple(int _sub_id, int _pre_id, int _obj_id)
{
    int* _objidlist = NULL;
    int _list_len = 0;
    (this->kvstore)->getobjIDlistBysubIDpreID(_sub_id, _pre_id, _objidlist, _list_len);

    bool is_exist = false;
//	for(int i = 0; i < _list_len; i ++)
//	{
//		if(_objidlist[i] == _obj_id)
//		{
//		    is_exist = true;
//			break;
//		}
//	}
    if (Util::bsearch_int_uporder(_obj_id, _objidlist, _list_len) != -1)
    {
        is_exist = true;
    }
    delete[] _objidlist;

    return is_exist;
}

/*
 * _rdf_file denotes the path of the RDF file, where stores the rdf data
 * there are many step will be finished in this function:
 * 1. assign tuples of RDF data with id, and store the map into KVstore
 * 2. build signature of each entity
 *
 * multi-thread implementation may save lots of time
 */
bool
Database::encodeRDF(const string _rdf_file)
{
    Database::log("In encodeRDF");
    int ** _p_id_tuples = NULL;
    int _id_tuples_max = 0;

    /* map sub2id and pre2id, storing in kvstore */
    this->sub2id_pre2id(_rdf_file, _p_id_tuples, _id_tuples_max);

    /* map literal2id, and encode RDF data into signature in the meantime */
    this->literal2id_RDFintoSignature(_rdf_file, _p_id_tuples, _id_tuples_max);

    /* map subid 2 objid_list  &
     * subIDpreID 2 objid_list &
     * subID 2 <preIDobjID>_list */
    this->s2o_sp2o_s2po(_p_id_tuples, _id_tuples_max);

    /* map objid 2 subid_list  &
     * objIDpreID 2 subid_list &
     * objID 2 <preIDsubID>_list */
    this->o2s_op2s_o2ps(_p_id_tuples, _id_tuples_max);

    bool flag = this->saveDBInfoFile();
    if (!flag)
    {
        return false;
    }

    Database::log("finish encodeRDF");

    return true;
}

bool
Database::encodeRDF_new(const string _rdf_file)
{
    Database::log("In encodeRDF_new");
    int ** _p_id_tuples = NULL;
    int _id_tuples_max = 0;

    /* map sub2id, pre2id, entity/literal in obj2id, store in kvstore, encode RDF data into signature */
    this->sub2id_pre2id_obj2id_RDFintoSignature(_rdf_file, _p_id_tuples, _id_tuples_max);

    /* map subid 2 objid_list  &
     * subIDpreID 2 objid_list &
     * subID 2 <preIDobjID>_list */
    this->s2o_sp2o_s2po(_p_id_tuples, _id_tuples_max);

    /* map objid 2 subid_list  &
     * objIDpreID 2 subid_list &
     * objID 2 <preIDsubID>_list */
    this->o2s_op2s_o2ps(_p_id_tuples, _id_tuples_max);

    bool flag = this->saveDBInfoFile();
    if (!flag)
    {
        return false;
    }

    Database::log("finish encodeRDF_new");

    return true;
}

bool
Database::sub2id_pre2id_obj2id_RDFintoSignature(const string _rdf_file, int**& _p_id_tuples, int & _id_tuples_max)
{
    int _id_tuples_size;
    {   /* initial */
        _id_tuples_max = 10*1000*1000;
        _p_id_tuples = new int*[_id_tuples_max];
        _id_tuples_size = 0;
        this->sub_num = 0;
        this->pre_num = 0;
        this->entity_num = 0;
        this->literal_num = 0;
        this->triples_num = 0;
        (this->kvstore)->open_entity2id(KVstore::CREATE_MODE);
        (this->kvstore)->open_id2entity(KVstore::CREATE_MODE);
        (this->kvstore)->open_predicate2id(KVstore::CREATE_MODE);
        (this->kvstore)->open_id2predicate(KVstore::CREATE_MODE);
        (this->kvstore)->open_literal2id(KVstore::CREATE_MODE);
        (this->kvstore)->open_id2literal(KVstore::CREATE_MODE);
    }

    Database::log("finish initial sub2id_pre2id_obj2id");


    ifstream _fin(_rdf_file.c_str());
    if(!_fin) {
        cerr << "sub2id&pre2id&obj2id: Fail to open : " << _rdf_file << endl;
        exit(0);
    }

    string _six_tuples_file = this->getSixTuplesFile();
    ofstream _six_tuples_fout(_six_tuples_file.c_str());
    if(! _six_tuples_fout) {
        cerr << "sub2id&pre2id&obj2id: Fail to open: " << _six_tuples_file << endl;
        exit(0);
    }

    TripleWithObjType* triple_array = new TripleWithObjType[RDFParser::TRIPLE_NUM_PER_GROUP];

    /*	don't know the number of entity
    *	pre allocate entitybitset_max EntityBitSet for storing signature, double the space until the _entity_bitset is used up.
    */
    int entitybitset_max = 10000;
    EntityBitSet** _entity_bitset = new EntityBitSet* [entitybitset_max];
    for(int i = 0; i < entitybitset_max; i ++)
    {
        _entity_bitset[i] = new EntityBitSet();
        _entity_bitset[i] -> reset();
    }
    EntityBitSet _tmp_bitset;

    //parse a file
    RDFParser _parser(_fin);

    Database::log("==> while(true)");

    while(true)
    {
        int parse_triple_num = 0;

        _parser.parseFile(triple_array, parse_triple_num);

        {
            stringstream _ss;
            _ss << "finish rdfparser" << this->triples_num << endl;
            Database::log(_ss.str());
            cout << _ss.str() << endl;
        }
        if(parse_triple_num == 0) {
            break;
        }

        /* Process the Triple one by one */
        for(int i = 0; i < parse_triple_num; i ++)
        {
            this->triples_num ++;

            /* if the _id_tuples exceeds, double the space */
            if(_id_tuples_size == _id_tuples_max) {
                int _new_tuples_len = _id_tuples_max * 2;
                int** _new_id_tuples = new int*[_new_tuples_len];
                memcpy(_new_id_tuples, _p_id_tuples, sizeof(int*) * _id_tuples_max);
                delete[] _p_id_tuples;
                _p_id_tuples = _new_id_tuples;
                _id_tuples_max = _new_tuples_len;
            }

            /*
             * For subject
             * (all subject is entity, some object is entity, the other is literal)
             * */
            string _sub = triple_array[i].getSubject();
            int _sub_id = (this->kvstore)->getIDByEntity(_sub);
            if(_sub_id == -1) {
                _sub_id = this->entity_num;
                (this->kvstore)->setIDByEntity(_sub, _sub_id);
                (this->kvstore)->setEntityByID(_sub_id, _sub);
                this->entity_num ++;
            }
            /*
             * For predicate
             * */
            string _pre = triple_array[i].getPredicate();
            int _pre_id = (this->kvstore)->getIDByPredicate(_pre);
            if(_pre_id == -1) {
                _pre_id = this->pre_num;
                (this->kvstore)->setIDByPredicate(_pre, _pre_id);
                (this->kvstore)->setPredicateByID(_pre_id, _pre);
                this->pre_num ++;
            }

            /*
            * For object
            * */
            string _obj = triple_array[i].getObject();
            int _obj_id = -1;
            // obj is entity
            if (triple_array[i].isObjEntity())
            {
                _obj_id = (this->kvstore)->getIDByEntity(_obj);
                if(_obj_id == -1)
                {
                    _obj_id = this->entity_num;
                    (this->kvstore)->setIDByEntity(_obj, _obj_id);
                    (this->kvstore)->setEntityByID(_obj_id, _obj);
                    this->entity_num ++;
                }
            }
            //obj is literal
            if (triple_array[i].isObjLiteral())
            {
                _obj_id = (this->kvstore)->getIDByLiteral(_obj);
                if(_obj_id == -1)
                {
                    _obj_id = Database::LITERAL_FIRST_ID + (this->literal_num);
                    (this->kvstore)->setIDByLiteral(_obj, _obj_id);
                    (this->kvstore)->setLiteralByID(_obj_id, _obj);
                    this->literal_num ++;
                }
            }

            /*
             * For id_tuples
             */
            _p_id_tuples[_id_tuples_size] = new int[3];
            _p_id_tuples[_id_tuples_size][0] = _sub_id;
            _p_id_tuples[_id_tuples_size][1] = _pre_id;
            _p_id_tuples[_id_tuples_size][2] = _obj_id;
            _id_tuples_size ++;

            /*
            *  save six tuples
            *  */
            {
                _six_tuples_fout << _sub_id << '\t'
                                 << _pre_id << '\t'
                                 << _obj_id << '\t'
                                 << _sub	<< '\t'
                                 << _pre << '\t'
                                 << _obj	<< endl;
            }

            //_entity_bitset is used up, double the space
            if (this->entity_num >= entitybitset_max)
            {
                EntityBitSet** _new_entity_bitset = new EntityBitSet* [entitybitset_max * 2];
                memcpy(_new_entity_bitset, _entity_bitset, sizeof(EntityBitSet*) * entitybitset_max);
                delete[] _entity_bitset;
                _entity_bitset = _new_entity_bitset;

                for(int i = entitybitset_max; i < entitybitset_max * 2; i ++)
                {
                    _entity_bitset[i] = new EntityBitSet();
                    _entity_bitset[i] -> reset();
                }

                entitybitset_max *= 2;
            }

            {
                _tmp_bitset.reset();
                Signature::encodePredicate2Entity(_pre_id, _tmp_bitset, BasicQuery::EDGE_OUT);
                Signature::encodeStr2Entity(_obj.c_str(), _tmp_bitset);
                *_entity_bitset[_sub_id] |= _tmp_bitset;
            }

            if(triple_array[i].isObjEntity())
            {
                _tmp_bitset.reset();
                Signature::encodePredicate2Entity(_pre_id, _tmp_bitset, BasicQuery::EDGE_IN);
                Signature::encodeStr2Entity(_sub.c_str(), _tmp_bitset);
                *_entity_bitset[_obj_id] |= _tmp_bitset;
            }
        }
    }

    Database::log("==> end while(true)");

    delete[] triple_array;
    _fin.close();
    _six_tuples_fout.close();
    (this->kvstore)->release();


    {   /* save all entity_signature into binary file */
        string _sig_binary_file = this->getSignatureBFile();
        FILE* _sig_fp = fopen(_sig_binary_file.c_str(), "wb");
        if(_sig_fp == NULL) {
            cerr << "Failed to open : " << _sig_binary_file << endl;
        }

        EntityBitSet _all_bitset;
        for(int i = 0; i < this->entity_num; i ++)
        {
            SigEntry* _sig = new SigEntry(EntitySig(*_entity_bitset[i]), i);

            fwrite(_sig, sizeof(SigEntry), 1, _sig_fp);
            _all_bitset |= *_entity_bitset[i];
            delete _sig;
        }
        fclose(_sig_fp);

        for (int i = 0; i < entitybitset_max; i++)
        {
            delete _entity_bitset[i];
        }
        delete[] _entity_bitset;
    }

    {
        stringstream _ss;
        _ss << "finish sub2id pre2id obj2id" << endl;
        _ss << "tripleNum is " << this->triples_num << endl;
        _ss << "entityNum is " << this->entity_num << endl;
        _ss << "preNum is " << this->pre_num << endl;
        _ss << "literalNum is " << this->literal_num << endl;
        Database::log(_ss.str());
        cout << _ss.str() << endl;
    }

    return true;
}


/*
 *	only after we determine the entityID(subid),
 *	we can determine the literalID(objid)
 */
bool
Database::sub2id_pre2id(const string _rdf_file, int**& _p_id_tuples, int & _id_tuples_max)
{
    int _id_tuples_size;;
    {   /* initial */
        _id_tuples_max = 10*1000*1000;
        _p_id_tuples = new int*[_id_tuples_max];
        _id_tuples_size = 0;
        this->sub_num = 0;
        this->pre_num = 0;
        this->triples_num = 0;
        (this->kvstore)->open_entity2id(KVstore::CREATE_MODE);
        (this->kvstore)->open_id2entity(KVstore::CREATE_MODE);
        (this->kvstore)->open_predicate2id(KVstore::CREATE_MODE);
        (this->kvstore)->open_id2predicate(KVstore::CREATE_MODE);
    }

    Database::log("finish initial sub2id_pre2id");
    {   /* map sub2id and pre2id */
        ifstream _fin(_rdf_file.c_str());
        if(!_fin) {
            cerr << "sub2id&pre2id: Fail to open : " << _rdf_file << endl;
            exit(0);
        }

        Triple* triple_array = new Triple[DBparser::TRIPLE_NUM_PER_GROUP];

        DBparser _parser;
        /* In while(true): For sub2id and pre2id.
         * parsed all RDF triples one group by one group
         * when parsed out an group RDF triples
         * for each triple
         * assign subject with subid, and predicate with preid
         * when get all sub2id,
         * we can assign object with objid in next while(true)
         * so that we can differentiate subject and object by their id
         *  */
        Database::log("==> while(true)");
        while(true)
        {
            int parse_triple_num = 0;
            _parser.rdfParser(_fin, triple_array, parse_triple_num);
            {
                stringstream _ss;
                _ss << "finish rdfparser" << this->triples_num << endl;
                Database::log(_ss.str());
                cout << _ss.str() << endl;
            }
            if(parse_triple_num == 0) {
                break;
            }

            /* Process the Triple one by one */
            for(int i = 0; i < parse_triple_num; i ++)
            {
                this->triples_num ++;

                /* if the _id_tuples exceeds, double the space */
                if(_id_tuples_size == _id_tuples_max) {
                    int _new_tuples_len = _id_tuples_max * 2;
                    int** _new_id_tuples = new int*[_new_tuples_len];
                    memcpy(_new_id_tuples, _p_id_tuples, sizeof(int*) * _id_tuples_max);
                    delete[] _p_id_tuples;
                    _p_id_tuples = _new_id_tuples;
                    _id_tuples_max = _new_tuples_len;
                }

                /*
                 * For subject
                 * (all subject is entity, some object is entity, the other is literal)
                 * */
                string _sub = triple_array[i].subject;
                int _sub_id = (this->kvstore)->getIDByEntity(_sub);
                if(_sub_id == -1) {
                    _sub_id = this->sub_num;
                    (this->kvstore)->setIDByEntity(_sub, _sub_id);
                    (this->kvstore)->setEntityByID(_sub_id, _sub);
                    this->sub_num ++;
                }
                /*
                 * For predicate
                 * */
                string _pre = triple_array[i].predicate;
                int _pre_id = (this->kvstore)->getIDByPredicate(_pre);
                if(_pre_id == -1) {
                    _pre_id = this->pre_num;
                    (this->kvstore)->setIDByPredicate(_pre, _pre_id);
                    (this->kvstore)->setPredicateByID(_pre_id, _pre);
                    this->pre_num ++;
                }

                /*
                 * For id_tuples
                 */
                _p_id_tuples[_id_tuples_size] = new int[3];
                _p_id_tuples[_id_tuples_size][0] = _sub_id;
                _p_id_tuples[_id_tuples_size][1] = _pre_id;
                _p_id_tuples[_id_tuples_size][2] = -1;
                _id_tuples_size ++;
            }
        }/* end while(true) for sub2id and pre2id */
        delete[] triple_array;
        _fin.close();
    }

    {   /* final process */
        this->entity_num = this->sub_num;
        (this->kvstore)->release();
    }

    {
        stringstream _ss;
        _ss << "finish sub2id pre2id" << endl;
        _ss << "tripleNum is " << this->triples_num << endl;
        _ss << "subNum is " << this->sub_num << endl;
        _ss << "preNum is " << this->pre_num << endl;
        Database::log(_ss.str());
        cout << _ss.str() << endl;
    }

    return true;
}

/* map literal2id and encode RDF data into signature in the meantime
 * literal id begin with Database::LITERAL_FIRST_ID */
bool
Database::literal2id_RDFintoSignature(const string _rdf_file, int** _p_id_tuples, int _id_tuples_max)
{
    Database::log("IN literal2id...");

    EntityBitSet* _entity_bitset = new EntityBitSet[this->sub_num];
    for(int i = 0; i < this->sub_num; i ++) {
        _entity_bitset[i].reset();
    }

    (this->kvstore)->open_id2literal(KVstore::CREATE_MODE);
    (this->kvstore)->open_literal2id(KVstore::CREATE_MODE);
    (this->kvstore)->open_entity2id(KVstore::READ_WRITE_MODE);

    /*  map obj2id */
    ifstream _fin(_rdf_file.c_str());
    if(!_fin) {
        cerr << "obj2id: Fail to open : " << _rdf_file << endl;
        exit(0);
    }

    string _six_tuples_file = this->getSixTuplesFile();
    ofstream _six_tuples_fout(_six_tuples_file.c_str());
    if(! _six_tuples_fout) {
        cerr << "obj2id: failed to open: " << _six_tuples_file << endl;
        exit(0);
    }

    Triple* triple_array = new Triple[DBparser::TRIPLE_NUM_PER_GROUP];

    DBparser _parser;
    this->entity_num = this->sub_num;
    int _i_tuples = 0;
    EntityBitSet _tmp_bitset;
    /* In while(true): For obj2id .
     * parsed all RDF triples one group by one group
     * when parsed out an group RDF triples
     * for each triple
     * assign object with objid
     *  */
    Database::log("literal2id: while(true)");
    while(true)
    {
        /* get next group of triples from rdfParser */
        int parse_triple_num = 0;
        _parser.rdfParser(_fin, triple_array, parse_triple_num);
        {
            stringstream _ss;
            _ss << "finish rdfparser" << _i_tuples << endl;
            Database::log(_ss.str());
            cout << _ss.str() << endl;
        }
        if(parse_triple_num == 0) {
            break;
        }

        /* Process the Triple one by one */
        for(int i = 0; i < parse_triple_num; i ++)
        {
            /*
             * For object(literal)
             * */
            string _obj = triple_array[i].object;
            /* check whether obj is an entity or not
             * if not, obj is a literal and assign it with a literal id */
            int _obj_id = (this->kvstore)->getIDByEntity(_obj);

            /* if obj is an literal */
            if(_obj_id == -1)
            {
                int _literal_id = (this->kvstore)->getIDByLiteral(_obj);
                /* if this literal does not exist before */
                if(_literal_id == -1)
                {
                    int _new_literal_id = Database::LITERAL_FIRST_ID + (this->literal_num);
                    (this->kvstore)->setIDByLiteral(_obj, _new_literal_id);
                    (this->kvstore)->setLiteralByID(_new_literal_id, _obj);
                    this->literal_num ++;
                    _obj_id = _new_literal_id;
                }
                else
                {
                    _obj_id = _literal_id;
                }
            }

//			{
//				stringstream _ss;
//				_ss << "object: " << _obj << " has id " << _obj_id << endl;
//				Database::log(_ss.str());
//			}

            _p_id_tuples[_i_tuples][2] = _obj_id;

            /*
             *  save six tuples
             *  */
            {
                _six_tuples_fout << _p_id_tuples[_i_tuples][0] << '\t'
                                 << _p_id_tuples[_i_tuples][1] << '\t'
                                 << _p_id_tuples[_i_tuples][2] << '\t'
                                 << triple_array[i].subject   << '\t'
                                 << triple_array[i].predicate << '\t'
                                 << triple_array[i].object    << endl;
            }

            /*
             * calculate entity signature
             */
            int _sub_id = _p_id_tuples[_i_tuples][0];
            int _pre_id = _p_id_tuples[_i_tuples][1];

            _tmp_bitset.reset();
            Signature::encodePredicate2Entity(_pre_id, _tmp_bitset, BasicQuery::EDGE_OUT);
            Signature::encodeStr2Entity((triple_array[i].object).c_str(), _tmp_bitset);
            _entity_bitset[_sub_id] |= _tmp_bitset;

            if(this->objIDIsEntityID(_obj_id))
            {
                _tmp_bitset.reset();
                Signature::encodePredicate2Entity(_pre_id, _tmp_bitset, BasicQuery::EDGE_IN);
                Signature::encodeStr2Entity((triple_array[i].subject).c_str(), _tmp_bitset);
                _entity_bitset[_obj_id] |= _tmp_bitset;
            }

            _i_tuples ++;
        }

    }/* end for while(true) */

    cout << "end for while" << endl;
    delete[] triple_array;
    _six_tuples_fout.close();
    _fin.close();

    (this->kvstore)->release();

    {   /* save all entity_signature into binary file */
        string _sig_binary_file = this->getSignatureBFile();
        FILE* _sig_fp = fopen(_sig_binary_file.c_str(), "wb");
        if(_sig_fp == NULL) {
            cerr << "Failed to open : " << _sig_binary_file << endl;
        }

        EntityBitSet _all_bitset;
        for(int i = 0; i < this->sub_num; i ++)
        {
            SigEntry* _sig = new SigEntry(EntitySig(_entity_bitset[i]), i);

            //debug
//			if(i == 0 || i == 2)
//			{
//				stringstream _ss;
//				_ss << "encodeRDF: " << i << " =" << _sig->getEntitySig().entityBitSet << endl;
//				Database::log(_ss.str());
//			}

            fwrite(_sig, sizeof(SigEntry), 1, _sig_fp);
            _all_bitset |= _entity_bitset[i];
            delete _sig;
        }
        fclose(_sig_fp);

        delete[] _entity_bitset;
    }

    Database::log("OUT literal2id...");

    return true;
}

/* map subid 2 objid_list  &
 * subIDpreID 2 objid_list &
 * subID 2 <preIDobjID>_list */
bool
Database::s2o_sp2o_s2po(int** _p_id_tuples, int _id_tuples_max)
{
    qsort(_p_id_tuples, this->triples_num, sizeof(int*), Database:: _spo_cmp);
    int* _oidlist_s = NULL;
    int* _oidlist_sp = NULL;
    int* _pidoidlist_s = NULL;
    int _oidlist_s_len = 0;
    int _oidlist_sp_len = 0;
    int _pidoidlist_s_len = 0;
    /* only _oidlist_s will be assigned with space
     * _oidlist_sp is always a part of _oidlist_s
     * just a pointer is enough
     *  */
    int _oidlist_max = 0;
    int _pidoidlist_max = 0;

    /* true means next sub is a different one from the previous one */
    bool _sub_change = true;

    /* true means next <sub,pre> is different from the previous pair */
    bool _sub_pre_change = true;

    /* true means next pre is different from the previous one */
    bool _pre_change = true;

    Database::log("finish s2p_sp2o_s2po initial");

    (this->kvstore)->open_subid2objidlist(KVstore::CREATE_MODE);
    (this->kvstore)->open_subIDpreID2objIDlist(KVstore::CREATE_MODE);
    (this->kvstore)->open_subID2preIDobjIDlist(KVstore::CREATE_MODE);

    for(int i = 0; i < this->triples_num; i ++)
    {
        if(_sub_change)
        {
            /* oidlist */
            _oidlist_max = 1000;
            _oidlist_s = new int[_oidlist_max];
            _oidlist_sp = _oidlist_s;
            _oidlist_s_len = 0;
            _oidlist_sp_len = 0;
            /* pidoidlist */
            _pidoidlist_max = 1000 * 2;
            _pidoidlist_s = new int[_pidoidlist_max];
            _pidoidlist_s_len = 0;
        }
        /* enlarge the space when needed */
        if(_oidlist_s_len == _oidlist_max)
        {
            _oidlist_max *= 10;
            int * _new_oidlist_s = new int[_oidlist_max];
            memcpy(_new_oidlist_s, _oidlist_s, sizeof(int) * _oidlist_s_len);
            /* (_oidlist_sp-_oidlist_s) is the offset of _oidlist_sp */
            _oidlist_sp = _new_oidlist_s + (_oidlist_sp-_oidlist_s);
            delete[] _oidlist_s;
            _oidlist_s = _new_oidlist_s;
        }

        /* enlarge the space when needed */
        if(_pidoidlist_s_len == _pidoidlist_max)
        {
            _pidoidlist_max *= 10;
            int* _new_pidoidlist_s = new int[_pidoidlist_max];
            memcpy(_new_pidoidlist_s, _pidoidlist_s, sizeof(int) * _pidoidlist_s_len);
            delete[] _pidoidlist_s;
            _pidoidlist_s = _new_pidoidlist_s;
        }

        int _sub_id = _p_id_tuples[i][0];
        int _pre_id = _p_id_tuples[i][1];
        int _obj_id = _p_id_tuples[i][2];
//		{
//			stringstream _ss;
//			_ss << _sub_id << "\t" << _pre_id << "\t" << _obj_id << endl;
//			Database::log(_ss.str());
//		}

        /* add objid to list */
        _oidlist_s[_oidlist_s_len] = _obj_id;

        /* if <subid, preid> changes, _oidlist_sp should be adjusted */
        if(_sub_pre_change) {
            _oidlist_sp = _oidlist_s + _oidlist_s_len;
        }

        _oidlist_s_len ++;
        _oidlist_sp_len ++;

        /* add <preid, objid> to list */
        _pidoidlist_s[_pidoidlist_s_len] = _pre_id;
        _pidoidlist_s[_pidoidlist_s_len+1] = _obj_id;
        _pidoidlist_s_len += 2;


        /* whether sub in new triple changes or not */
        _sub_change = (i+1 == this->triples_num) ||
                      (_p_id_tuples[i][0] != _p_id_tuples[i+1][0]);

        /* whether pre in new triple changes or not */
        _pre_change = (i+1 == this->triples_num) ||
                      (_p_id_tuples[i][1] != _p_id_tuples[i+1][1]);

        /* whether <sub,pre> in new triple changes or not */
        _sub_pre_change = _sub_change || _pre_change;

        if(_sub_pre_change)
        {
            (this->kvstore)->setobjIDlistBysubIDpreID(_sub_id, _pre_id, _oidlist_sp, _oidlist_sp_len);
            _oidlist_sp = NULL;
            _oidlist_sp_len = 0;
        }

        if(_sub_change)
        {
            /* map subid 2 objidlist */
            Util::sort(_oidlist_s, _oidlist_s_len);
            (this->kvstore)->setobjIDlistBysubID(_sub_id, _oidlist_s, _oidlist_s_len);
            delete[] _oidlist_s;
            _oidlist_s = NULL;
            _oidlist_sp = NULL;
            _oidlist_s_len = 0;

            /* map subid 2 preid&objidlist */
            (this->kvstore)->setpreIDobjIDlistBysubID(_sub_id, _pidoidlist_s, _pidoidlist_s_len);
            delete[] _pidoidlist_s;
            _pidoidlist_s = NULL;
            _pidoidlist_s_len = 0;
        }

    }/* end for( 0 to this->triple_num)  */

    (this->kvstore)->release();

    Database::log("OUT s2po...");

    return true;
}

/* map objid 2 subid_list  &
 * objIDpreID 2 subid_list &
 * objID 2 <preIDsubID>_list */
bool
Database::o2s_op2s_o2ps(int** _p_id_tuples, int _id_tuples_max)
{
    Database::log("IN o2ps...");

    qsort(_p_id_tuples, this->triples_num, sizeof(int**), Database::_ops_cmp);
    int* _sidlist_o = NULL;
    int* _sidlist_op = NULL;
    int* _pidsidlist_o = NULL;
    int _sidlist_o_len = 0;
    int _sidlist_op_len = 0;
    int _pidsidlist_o_len = 0;
    /* only _sidlist_o will be assigned with space
     * _sidlist_op is always a part of _sidlist_o
     * just a pointer is enough */
    int _sidlist_max = 0;
    int _pidsidlist_max = 0;

    /* true means next obj is a different one from the previous one */
    bool _obj_change = true;

    /* true means next <obj,pre> is different from the previous pair */
    bool _obj_pre_change = true;

    /* true means next pre is a different one from the previous one */
    bool _pre_change = true;

    (this->kvstore)->open_objid2subidlist(KVstore::CREATE_MODE);
    (this->kvstore)->open_objIDpreID2subIDlist(KVstore::CREATE_MODE);
    (this->kvstore)->open_objID2preIDsubIDlist(KVstore::CREATE_MODE);

    for(int i = 0; i < this->triples_num; i ++)
    {
        if(_obj_change)
        {
            /* sidlist */
            _sidlist_max = 1000;
            _sidlist_o = new int[_sidlist_max];
            _sidlist_op = _sidlist_o;
            _sidlist_o_len = 0;
            _sidlist_op_len = 0;
            /* pidsidlist */
            _pidsidlist_max = 1000 * 2;
            _pidsidlist_o = new int[_pidsidlist_max];
            _pidsidlist_o_len = 0;
        }
        /* enlarge the space when needed */
        if(_sidlist_o_len == _sidlist_max)
        {
            _sidlist_max *= 10;
            int * _new_sidlist_o = new int[_sidlist_max];
            memcpy(_new_sidlist_o, _sidlist_o, sizeof(int)*_sidlist_o_len);
            /* (_sidlist_op-_sidlist_o) is the offset of _sidlist_op */
            _sidlist_op = _new_sidlist_o + (_sidlist_op-_sidlist_o);
            delete[] _sidlist_o;
            _sidlist_o = _new_sidlist_o;
        }

        /* enlarge the space when needed */
        if(_pidsidlist_o_len == _pidsidlist_max)
        {
            _pidsidlist_max *= 10;
            int* _new_pidsidlist_o = new int[_pidsidlist_max];
            memcpy(_new_pidsidlist_o, _pidsidlist_o, sizeof(int) * _pidsidlist_o_len);
            delete[] _pidsidlist_o;
            _pidsidlist_o = _new_pidsidlist_o;
        }

        int _sub_id = _p_id_tuples[i][0];
        int _pre_id = _p_id_tuples[i][1];
        int _obj_id = _p_id_tuples[i][2];

        /* add subid to list */
        _sidlist_o[_sidlist_o_len] = _sub_id;

        /* if <objid, preid> changes, _sidlist_op should be adjusted */
        if(_obj_pre_change) {
            _sidlist_op = _sidlist_o + _sidlist_o_len;
        }

        _sidlist_o_len ++;
        _sidlist_op_len ++;

        /* add <preid, subid> to list */
        _pidsidlist_o[_pidsidlist_o_len] = _pre_id;
        _pidsidlist_o[_pidsidlist_o_len+1] = _sub_id;;
        _pidsidlist_o_len += 2;

        /* whether sub in new triple changes or not */
        _obj_change = (i+1 == this->triples_num) ||
                      (_p_id_tuples[i][2] != _p_id_tuples[i+1][2]);

        /* whether pre in new triple changes or not */
        _pre_change = (i+1 == this->triples_num) ||
                      (_p_id_tuples[i][1] != _p_id_tuples[i+1][1]);

        /* whether <sub,pre> in new triple changes or not */
        _obj_pre_change = _obj_change || _pre_change;

        if(_obj_pre_change)
        {
            (this->kvstore)->setsubIDlistByobjIDpreID(_obj_id, _pre_id, _sidlist_op, _sidlist_op_len);
            _sidlist_op = NULL;
            _sidlist_op_len = 0;
        }

        if(_obj_change)
        {
            /* map objid 2 subidlist */
            Util::sort(_sidlist_o, _sidlist_o_len);
            (this->kvstore)->setsubIDlistByobjID(_obj_id, _sidlist_o, _sidlist_o_len);
            delete[] _sidlist_o;
            _sidlist_o = NULL;
            _sidlist_op = NULL;
            _sidlist_o_len = 0;

            /* map objid 2 preid&subidlist */
            (this->kvstore)->setpreIDsubIDlistByobjID(_obj_id, _pidsidlist_o, _pidsidlist_o_len);
            delete[] _pidsidlist_o;
            _pidsidlist_o = NULL;
            _pidsidlist_o_len = 0;
        }

    }/* end for( 0 to this->triple_num)  */

    (this->kvstore)->release();

    Database::log("OUT o2ps...");

    return true;
}

int
Database::insertTriple(const TripleWithObjType& _triple)
{
    //long tv_kv_store_begin = Util::get_cur_time();

    int _sub_id = (this->kvstore)->getIDByEntity(_triple.subject);
    bool _is_new_sub = false;
    /* if sub does not exist */
    if(_sub_id == -1)
    {
        _is_new_sub = true;
        _sub_id = this->entity_num ++;;
        (this->kvstore)->setIDByEntity(_triple.subject, _sub_id);
        (this->kvstore)->setEntityByID(_sub_id, _triple.subject);
    }

    int _pre_id = (this->kvstore)->getIDByPredicate(_triple.predicate);
    bool _is_new_pre = false;
    if(_pre_id == -1)
    {
        _is_new_pre = true;
        _pre_id = this->pre_num ++;
        (this->kvstore)->setIDByPredicate(_triple.predicate, _pre_id);
        (this->kvstore)->setPredicateByID(_pre_id, _triple.predicate);
    }

    /* object is either entity or literal */
    int _obj_id = -1;
    bool _is_new_obj = false;
    bool _is_obj_entity = _triple.isObjEntity();
    if (_is_obj_entity)
    {
        _obj_id = (this->kvstore)->getIDByEntity(_triple.object);

        if (_obj_id == -1)
        {
            _is_new_obj = true;
            _obj_id = this->entity_num ++;
            (this->kvstore)->setIDByEntity(_triple.object, _obj_id);
            (this->kvstore)->setEntityByID(_obj_id, _triple.object);
        }
    }
    else
    {
        _obj_id = (this->kvstore)->getIDByLiteral(_triple.object);

        if (_obj_id == -1)
        {
            _is_new_obj = true;
            _obj_id = Database::LITERAL_FIRST_ID + this->literal_num;
            this->literal_num ++;
            (this->kvstore)->setIDByLiteral(_triple.object, _obj_id);
            (this->kvstore)->setLiteralByID(_obj_id, _triple.object);
        }
    }

    /* if this is not a new triple, return directly */
    bool _triple_exist = false;
    if(!_is_new_sub &&
            !_is_new_pre &&
            !_is_new_obj   )
    {
        _triple_exist = this->exist_triple(_sub_id, _pre_id, _obj_id);
    }

    //debug
//  {
//      stringstream _ss;
//      _ss << this->literal_num << endl;
//      _ss <<"ids: " << _sub_id << " " << _pre_id << " " << _obj_id << " " << _triple_exist << endl;
//      Database::log(_ss.str());
//  }

    if(_triple_exist)
    {
        return 0;
    }
    else
    {
        this->triples_num ++;
    }

    /* update sp2o op2s s2po o2ps s2o o2s */
    int updateLen = (this->kvstore)->updateTupleslist_insert(_sub_id, _pre_id, _obj_id);

    //long tv_kv_store_end = Util::get_cur_time();

    EntityBitSet _sub_entity_bitset;
    _sub_entity_bitset.reset();

    this->encodeTriple2SubEntityBitSet(_sub_entity_bitset, &_triple);

    /* if new entity then insert it, else update it. */
    if(_is_new_sub)
    {
        SigEntry _sig(_sub_id, _sub_entity_bitset);
        (this->vstree)->insertEntry(_sig);
    }
    else
    {
        (this->vstree)->updateEntry(_sub_id, _sub_entity_bitset);
    }

    /* if the object is an entity, then update or insert this entity's entry. */
    if (_is_obj_entity)
    {
        EntityBitSet _obj_entity_bitset;
        _obj_entity_bitset.reset();

        this->encodeTriple2ObjEntityBitSet(_obj_entity_bitset, &_triple);

        if (_is_new_obj)
        {
            SigEntry _sig(_obj_id, _obj_entity_bitset);
            (this->vstree)->insertEntry(_sig);
        }
        else
        {
            (this->vstree)->updateEntry(_obj_id, _obj_entity_bitset);
        }
    }

    //long tv_vs_store_end = Util::get_cur_time();

    //debug
//    {
//        cout << "update kv_store, used " << (tv_kv_store_end - tv_kv_store_begin) << "ms." << endl;
//        cout << "update vs_store, used " << (tv_vs_store_end - tv_kv_store_end) << "ms." << endl;
//    }

    return updateLen;
}

// need debug and test...
bool
Database::removeTriple(const TripleWithObjType& _triple)
{
    int _sub_id = (this->kvstore)->getIDByEntity(_triple.subject);
    int _pre_id = (this->kvstore)->getIDByPredicate(_triple.predicate);
    int _obj_id = (this->kvstore)->getIDByEntity(_triple.object);
    if(_obj_id == -1) {
        _obj_id = (this->kvstore)->getIDByLiteral(_triple.object);
    }

    if(_sub_id == -1 || _pre_id == -1 || _obj_id == -1)
    {
        return false;
    }
    bool _exist_triple = this->exist_triple(_sub_id, _pre_id, _obj_id);
    if(! _exist_triple)
    {
        return false;
    }

    /* remove from sp2o op2s s2po o2ps s2o o2s
     * sub2id, pre2id and obj2id will not be updated */
    (this->kvstore)->updateTupleslist_remove(_sub_id, _pre_id, _obj_id);


    int _sub_degree = (this->kvstore)->getEntityDegree(_sub_id);

    /* if subject become an isolated point, remove its corresponding entry */
    if(_sub_degree == 0)
    {
        (this->vstree)->removeEntry(_sub_id);
    }
    /* else re-calculate the signature of subject & replace that in vstree */
    else
    {
        EntityBitSet _entity_bitset;
        _entity_bitset.reset();
        this->calculateEntityBitSet(_sub_id, _entity_bitset);
        (this->vstree)->replaceEntry(_sub_id, _entity_bitset);
    }

    return true;
}

/* compare function for qsort */
int
Database::_spo_cmp(const void* _a, const void* _b)
{
    int** _p_a = (int**)_a;
    int** _p_b = (int**)_b;

    {   /* compare subid first */
        int _sub_id_a = (*_p_a)[0];
        int _sub_id_b = (*_p_b)[0];
        if(_sub_id_a != _sub_id_b)
        {
            return _sub_id_a - _sub_id_b;
        }
    }

    {   /* then preid */
        int _pre_id_a = (*_p_a)[1];
        int _pre_id_b = (*_p_b)[1];
        if(_pre_id_a != _pre_id_b)
        {
            return _pre_id_a - _pre_id_b;
        }
    }

    {   /* objid at last */
        int _obj_id_a = (*_p_a)[2];
        int _obj_id_b = (*_p_b)[2];
        if(_obj_id_a != _obj_id_b)
        {
            return _obj_id_a - _obj_id_b;
        }
    }
    return 0;
}

/* compare function for qsort */
int
Database::_ops_cmp(const void* _a, const void* _b)
{
    int** _p_a = (int**)_a;
    int** _p_b = (int**)_b;
    {   /* compare objid first */
        int _obj_id_a = (*_p_a)[2];
        int _obj_id_b = (*_p_b)[2];
        if(_obj_id_a != _obj_id_b)
        {
            return _obj_id_a - _obj_id_b;
        }
    }

    {   /* then preid */
        int _pre_id_a = (*_p_a)[1];
        int _pre_id_b = (*_p_b)[1];
        if(_pre_id_a != _pre_id_b)
        {
            return _pre_id_a - _pre_id_b;
        }
    }

    {   /* subid at last */
        int _sub_id_a = (*_p_a)[0];
        int _sub_id_b = (*_p_b)[0];
        if(_sub_id_a != _sub_id_b)
        {
            return _sub_id_a - _sub_id_b;
        }
    }

    return 0;
}

bool
Database::objIDIsEntityID(int _id)
{
    return _id < Database::LITERAL_FIRST_ID;
}

bool
Database::join(vector<int*>& _result_list, int _var_id, int _pre_id, \
               int _var_id2, const char _edge_type, int _var_num, \
               bool shouldAddLiteral, IDList& _can_list, bool* _dealed_internal_arr)
{
    int* id_list;
    int id_list_len;
    vector<int*> new_result_list;

    vector<int*>::iterator itr = _result_list.begin();

    bool has_preid = (_pre_id >= 0);
    for ( ; itr != _result_list.end(); itr++)
    {
        int* itr_result = (*itr);
		//if the mapping of the _var_id is not an internal vertex, do not join
		if(itr_result[_var_id] == -1)
		{
			new_result_list.push_back(itr_result);
			continue;
		}
		
		if(itr_result[_var_id] < Database::LITERAL_FIRST_ID)
		{
			if ('0' == this->internal_tag_arr.at(itr_result[_var_id]))
			{
				new_result_list.push_back(itr_result);
				continue;
			}
		}

        if (_can_list.size() == 0 && !shouldAddLiteral)
        {
            itr_result[_var_num] = -1;
            continue;
        }

        if (has_preid)
        {
            if (_edge_type == BasicQuery::EDGE_IN)
            {
                kvstore->getsubIDlistByobjIDpreID(itr_result[_var_id],
                                                  _pre_id, id_list, id_list_len);
            }
            else
            {
                kvstore->getobjIDlistBysubIDpreID(itr_result[_var_id],
                                                  _pre_id, id_list, id_list_len);
            }

        }
        else
            /* pre_id == -1 means we cannot find such predicate in rdf file, so the result set of this sparql should be empty.
             * note that we cannot support to query sparqls with predicate variables ?p.
             */
        {
			if (_edge_type == BasicQuery::EDGE_IN)
			{
				kvstore->getsubIDlistByobjID(itr_result[_var_id],
						id_list, id_list_len);
			}
			else
			{
				kvstore->getobjIDlistBysubID(itr_result[_var_id],
						id_list, id_list_len);
			}
        }

        if (id_list_len == 0)
        {
            itr_result[_var_num] = -1;
            continue;
        }
		
        stringstream _tmp_ss;
        for (int i = 0; i < id_list_len; i++)
        {
			if(id_list[i] >= Database::LITERAL_FIRST_ID){
				int* result = new int[_var_num + 1];
				memcpy(result, itr_result,
					   sizeof(int) * (_var_num + 1));
				result[_var_id2] = id_list[i];
				new_result_list.push_back(result);
				continue;
			}
			
			if ('0' == this->internal_tag_arr.at(id_list[i]))
			{
				int* result = new int[_var_num + 1];
				memcpy(result, itr_result,
					   sizeof(int) * (_var_num + 1));
				result[_var_id2] = id_list[i];
				new_result_list.push_back(result);
				continue;
			}
			
			if(_dealed_internal_arr[_var_id2]){
				continue;
			}

            bool found_in_id_list = _can_list.bsearch_uporder(id_list[i]) >= 0;
            bool should_add_this_literal = shouldAddLiteral && !this->objIDIsEntityID(id_list[i]);

            // if we found this element(entity/literal) in var1's candidate list,
            // or this is a literal element and var2 is a free literal variable,
            // we should add this one to result array.
            if (found_in_id_list || should_add_this_literal)
            {
				int* result = new int[_var_num + 1];
				memcpy(result, itr_result,
					   sizeof(int) * (_var_num + 1));
				result[_var_id2] = id_list[i];
				new_result_list.push_back(result);
            }
        }

        delete[] id_list;
    }
	
	//cout << "after join=========" << new_result_list.size() << endl;
	_result_list.assign(new_result_list.begin(), new_result_list.end());
	
	int invalid_num = 0;
    for(unsigned i = 0; i < _result_list.size(); i ++)
    {
        if(_result_list[i][_var_num] == -1)
        {
            invalid_num ++;
        }
    }

    //cout << "*****Join done" << endl;
    return true;
}

bool
Database::join(vector<int*>& _result_list, int _var_id, int _pre_id, \
               int _var_id2, const char _edge_type, int _var_num, \
               bool shouldAddLiteral, IDList& _can_list)
{
    int* id_list;
    int id_list_len;
    vector<int*> new_result_list;

    vector<int*>::iterator itr = _result_list.begin();

    bool has_preid = (_pre_id >= 0);
    for ( ; itr != _result_list.end(); itr++)
    {
        int* itr_result = (*itr);
		//if the mapping of the _var_id is not an internal vertex, do not join
		if(itr_result[_var_id] == -1)
		{
			new_result_list.push_back(itr_result);
			continue;
		}
		
		if(itr_result[_var_id] < Database::LITERAL_FIRST_ID)
		{
			if ('0' == this->internal_tag_arr.at(itr_result[_var_id]))
			{
				new_result_list.push_back(itr_result);
				continue;
			}
		}

		/*
		cout << "shouldAddLiteral is " << shouldAddLiteral << endl;
        if (_can_list.size()==0 && !shouldAddLiteral)
        {
            itr_result[_var_num] = -1;
            continue;
        }
		*/

        if (has_preid)
        {
            if (_edge_type == BasicQuery::EDGE_IN)
            {
                kvstore->getsubIDlistByobjIDpreID(itr_result[_var_id],
                                                  _pre_id, id_list, id_list_len);
            }
            else
            {
                kvstore->getobjIDlistBysubIDpreID(itr_result[_var_id],
                                                  _pre_id, id_list, id_list_len);
            }

        }
        else
            /* pre_id == -1 means we cannot find such predicate in rdf file, so the result set of this sparql should be empty.
             * note that we cannot support to query sparqls with predicate variables ?p.
             */
        {
            id_list_len = 0;
        }

        if (id_list_len == 0)
        {
            itr_result[_var_num] = -1;
            continue;
        }
		
        stringstream _tmp_ss;
        for (int i = 0; i < id_list_len; i++)
        {
			if(id_list[i] >= Database::LITERAL_FIRST_ID){
				int* result = new int[_var_num + 1];
				memcpy(result, itr_result,
					   sizeof(int) * (_var_num + 1));
				result[_var_id2] = id_list[i];
				new_result_list.push_back(result);
				continue;
			}
			
			if ('0' == this->internal_tag_arr.at(id_list[i]))
			{
				int* result = new int[_var_num + 1];
				memcpy(result, itr_result,
					   sizeof(int) * (_var_num + 1));
				result[_var_id2] = id_list[i];
				new_result_list.push_back(result);
				continue;
			}

            bool found_in_id_list = _can_list.bsearch_uporder(id_list[i]) >= 0;
            bool should_add_this_literal = shouldAddLiteral && !this->objIDIsEntityID(id_list[i]);

            // if we found this element(entity/literal) in var1's candidate list,
            // or this is a literal element and var2 is a free literal variable,
            // we should add this one to result array.
            if (found_in_id_list || should_add_this_literal)
            {
                int* result = new int[_var_num + 1];
				memcpy(result, itr_result,
					   sizeof(int) * (_var_num + 1));
				result[_var_id2] = id_list[i];
				new_result_list.push_back(result);
            }
        }

        delete[] id_list;
    }
	
	//cout << "after join=========" << new_result_list.size() << endl;
	_result_list.assign(new_result_list.begin(), new_result_list.end());
	
	int invalid_num = 0;
    for(unsigned i = 0; i < _result_list.size(); i ++)
    {
        if(_result_list[i][_var_num] == -1)
        {
            invalid_num ++;
        }
    }

    //cout << "*****Join done" << endl;
    return true;
}

bool Database::select(vector<int*>& _result_list,int _var_id,int _pre_id,int _var_id2,const char _edge_type,int _var_num)
{
    //cout << "*****In select" << endl;

    int* id_list;
    int id_list_len;
	vector<int*> new_result_list;

    vector<int*>::iterator itr = _result_list.begin();
    for ( ;	itr != _result_list.end(); itr++)
    {
        int* itr_result = (*itr);
		
		//if the endpoints of the mapping edges are two internal vertices, do not select
		//cout << itr_result[_var_id] << "\t" << Database::LITERAL_FIRST_ID << "\t" << itr_result[_var_id2] << "\t" << this->internal_tag_arr.size() << endl;
		if(itr_result[_var_id] == -1){
			if(itr_result[_var_id2] < Database::LITERAL_FIRST_ID && '0' == this->internal_tag_arr.at(itr_result[_var_id2])){
				new_result_list.push_back(itr_result);
			}
			continue;
		}
		if(itr_result[_var_id2] == -1){
			if(itr_result[_var_id] < Database::LITERAL_FIRST_ID && '0' == this->internal_tag_arr.at(itr_result[_var_id])){
				new_result_list.push_back(itr_result);
			}
			continue;
		}

		if ('0' == this->internal_tag_arr.at(itr_result[_var_id]) && '0' == this->internal_tag_arr.at(itr_result[_var_id2]))
		{
			new_result_list.push_back(itr_result);
			continue;
		}
		
        //if (itr_result[_var_num] == -1)
        //{
        //    continue;
        //}

        //bool ret = false;
        if (_pre_id >= 0)
        {
            if (_edge_type == BasicQuery::EDGE_IN)
            {
                kvstore->getsubIDlistByobjIDpreID(itr_result[_var_id],
                                                  _pre_id, id_list, id_list_len);
            }
            else
            {
                kvstore->getobjIDlistBysubIDpreID(itr_result[_var_id],
                                                  _pre_id, id_list, id_list_len);

            }
        }
        else
            /* pre_id == -1 means we cannot find such predicate in rdf file, so the result set of this sparql should be empty.
             * note that we cannot support to query sparqls with predicate variables ?p.
             */
        {
			if (_edge_type == BasicQuery::EDGE_IN)
			{
				kvstore->getsubIDlistByobjID(itr_result[_var_id],
						id_list, id_list_len);
			}
			else
			{
				kvstore->getobjIDlistBysubID(itr_result[_var_id],
						id_list, id_list_len);
			}
        }

        if (id_list_len == 0)
        {
            //itr_result[_var_num] = -1;
            continue;
        }

        if (Util::bsearch_int_uporder(itr_result[_var_id2], id_list,
                                      id_list_len) == -1)
        {
            itr_result[_var_num] = -1;
        }else{
			new_result_list.push_back(itr_result);
		}
        delete[] id_list;
    }

	_result_list.assign(new_result_list.begin(), new_result_list.end());
    int invalid_num = 0;
    for(unsigned i = 0; i < _result_list.size(); i ++)
    {
        if(_result_list[i][_var_num] == -1)
        {
            invalid_num ++;
        }
    }

    //cout << "\t\tresult size: " << _result_list.size() << " invalid:" << invalid_num << endl;
//
    //cout << "*****Select done" << endl;
    return true;
}

//join on the vector of CandidateList, available after retrieved from the VSTREE
//and store the resut in _result_set
int
Database::join(SPARQLquery& _sparql_query, int myRank, string& res_char_arr)
{
    int basic_query_num = _sparql_query.getBasicQueryNum();
	string res_ss;
	int var_num = 0;
    //join each basic query
    for(int i=0; i < basic_query_num; i++)
    {
        //cout<<"Basic query "<<i<<endl;
        BasicQuery* basic_query;
        basic_query=&(_sparql_query.getBasicQuery(i));
		var_num = basic_query->getVarNum();
        long begin = Util::get_cur_time();
        this->filter_before_join(basic_query);
        long after_filter = Util::get_cur_time();
        //cout << "after filter_before_join: used " << (after_filter-begin) << " ms" << endl;
        this->add_literal_candidate(basic_query);
        long after_add_literal = Util::get_cur_time();
        //cout << "after add_literal_candidate: used " << (after_add_literal-after_filter) << " ms" << endl;
        //this->join_pe(basic_query, res_char_arr);
        long after_joinbasic = Util::get_cur_time();
        
        // remove invalid and duplicate result at the end.
		//printf("There are %d partial results in Client %d!\n", (basic_query->getResultList()).size(), myRank);
    }
    return var_num;
}

int
Database::joinASK(SPARQLquery& _sparql_query, int myRank, string& res_char_arr)//, string& query_garph)
{
    int basic_query_num = _sparql_query.getBasicQueryNum();
	string query_ss;
	int var_num = 0;
    //join each basic query
    for(int i = 0; i < basic_query_num; i++)
    {
        //cout<<"Basic query "<<i<<endl;
        BasicQuery* basic_query;
        basic_query=&(_sparql_query.getBasicQuery(i));
		var_num = basic_query->getVarNum();
        long begin = Util::get_cur_time();
        this->filter_before_join(basic_query);
        long after_filter = Util::get_cur_time();
        //cout << "after filter_before_join: used " << (after_filter-begin) << " ms" << endl;
        this->add_literal_candidate(basic_query);
        long after_add_literal = Util::get_cur_time();
        //cout << "after add_literal_candidate: used " << (after_add_literal-after_filter) << " ms" << endl;
        this->join_pe_ask(basic_query, res_char_arr);
        long after_joinbasic = Util::get_cur_time();
		
		//printf("There are %d partial results in Client %d!\n", (basic_query->getResultList()).size(), myRank);
		/*
		for(int j = 0; j < var_num; j++){
			int var_degree = basic_query->getVarDegree(j);
			for (int k = 0; k < var_degree; k++)
			{
				// each triple/edge need to be processed only once.
				int edge_id = basic_query->getEdgeID(j, k);				
				int var_id2 = basic_query->getEdgeNeighborID(j, k);
				if (var_id2 == -1)
				{
					continue;
				}
				
				if(j <= var_id2){
					query_ss << j << " " << var_id2 << endl;
				}
			}
		}
		*/
    }
	//query_garph = query_ss.str();
	
    return var_num;
}

string
Database::printResList(vector<int*>* _res_list, int _len, string _offset)
{
	stringstream _ss;
	for(int i = 0; i < _res_list->size(); i++){
		int* result_var = _res_list->at(i);
		_ss << _offset;
		for(int j = 0; j < _len; j++){
			if(result_var[j] != -1){
				string _tmp = (this->kvstore)->getEntityByID(result_var[j]);
				if(_tmp.compare("") != 0){
					_ss << this->internal_tag_arr.at(result_var[j]) << " " << result_var[j] << " " << _tmp << " ";
				}else{
					_ss << "1 " << result_var[j] << " " << this->kvstore->getLiteralByID(result_var[j]) << " ";
				}
			}else{
				_ss << "-1 ";
			}
		}
		_ss << endl;
	}
	return _ss.str();
}

void
Database::ResListtoString(vector<int*>* _res_list, int _len, string& res_char_arr)
{
	stringstream res_ss;  
	for(vector<int*>::iterator it = _res_list->begin(); it != _res_list->end(); it++){
		//res_ss << _offset;
		int* result_var = *it;
		for(int j = 0; j < _len; j++){
			if(result_var[j] != -1){
				string _tmp = (this->kvstore)->getEntityByID(result_var[j]);
				if(_tmp.compare("") != 0){
					res_ss << this->internal_tag_arr.at(result_var[j]) << _tmp << "\t";
					//cout << _tmp << "\t";
				}else{
					res_ss << "1" << (this->kvstore)->getLiteralByID(result_var[j]) << "\t";
					//cout << (this->kvstore)->getLiteralByID(result_var[j]) << "\t";
				}
			}else{
				res_ss << "-1\t";
				//cout << "-1\t";
			}
		}
		res_ss << endl;
		//cout << endl;
	}
	
	res_char_arr = res_ss.str();
	//cout << "=======+++++" << res_char_arr << endl;
}

string
Database::printDealedVertices(bool* _id_list, int _len)
{
	stringstream ss;
	for(int i = 0; i < _len; i++)
	{
		ss << _id_list[i];
	}
	
	return ss.str();
}

bool
Database::join_pe(BasicQuery* basic_query, string& res)
{
    //cout << "IIIIIIN join partial evaluation" << endl;

    int var_num = basic_query->getVarNum();
    int triple_num = basic_query->getTripleNum();

    // initial p_result_list, push min_var_list in
	vector<int*>* all_result_list = &basic_query->getResultList();
	//set<int*>* all_result_set;
	all_result_list->clear();
	//set<string> dealed_subquery;
	
	//ofstream log_output("log_intermedidate.txt");
	bool* dealed_internal_id_list = new bool[var_num];
	memset(dealed_internal_id_list, 0, sizeof(bool) * var_num);
	
	for(int res_j = 0; res_j < var_num; res_j++){
		int var_degree = basic_query->getVarDegree(res_j);
		//bool join_tag = false, processing_tag = false;
		for (int i = 0; i < var_degree; i++)
		{
			// each triple/edge need to be processed only once.
			int edge_id = basic_query->getEdgeID(res_j, i);
			
			int var_id2 = basic_query->getEdgeNeighborID(res_j, i);
			if (var_id2 == -1)
			{
				continue;
			}
			
			//processing_tag = true;//if do not join or select, we do not clear the result list
			Triple curTriple = basic_query->getTriple(edge_id);
			int pre_id = basic_query->getEdgePreID(res_j, i);//predicate id
			
			if(curTriple.getPredicate().at(0) != '?' && pre_id == -1){
				return false;
			}
		}
	}

	for(int res_j = 0; res_j < var_num; res_j++){
	
		//mark dealed_id_list and dealed_triple, 0 not processed, 1 for processed
		bool* dealed_id_list = new bool[var_num];
		bool* dealed_triple = new bool[triple_num];
		memset(dealed_id_list, 0, sizeof(bool) * var_num);
		memset(dealed_triple, 0, sizeof(bool) * triple_num);

		int start_var_id = res_j;
		int start_var_size = basic_query->getCandidateSize(start_var_id);
		//cout << "--------------variable " << res_j << " of " << var_num << " has " << start_var_size << " candidates" << endl;
		
		//start_var_size == 0  no answer in this basic query
		if (start_var_size == 0)
		{
			continue;
		}

		vector<int*>* p_result_list = new vector<int*>;
		IDList* p_min_var_list = &(basic_query->getCandidateList(start_var_id));
		for (int i = 0; i < start_var_size; i++)
		{
			int* result_var = new int[var_num + 1];
			memset(result_var, -1, sizeof(int) * (var_num + 1));
			result_var[start_var_id] = p_min_var_list->getID(i);
			p_result_list->push_back(result_var);
		}
		
		stack<int> var_stack;
		var_stack.push(start_var_id);
		dealed_id_list[start_var_id] = true;
		//log_output << "start_var_id = " << start_var_id << endl;
		//dealed_internal_id_list[start_var_id] = true;
		//dealed_subquery.insert(this->printDealedVertices(dealed_internal_id_list, var_num));
		/*
		for(set<string>::iterator it = dealed_subquery.begin(); it != dealed_subquery.end(); it++){
			cout << *it << ";";
		}
		cout << endl;
		*/
		while (!var_stack.empty())
		{
			int var_id = var_stack.top();
			
			stringstream ss;
			/*
			ss << var_stack.size() << "-" << var_id << "\t";
			string output_str = this->printResList(p_result_list, var_num + 1, ss.str());
			log_output << output_str << endl;
			*/
			var_stack.pop();
			
			int var_degree = basic_query->getVarDegree(var_id);
			//bool join_tag = false, processing_tag = false;
			for (int i = 0; i < var_degree; i++)
			{
				// each triple/edge need to be processed only once.
				int edge_id = basic_query->getEdgeID(var_id, i);
				if (dealed_triple[edge_id])
				{
					continue;
				}
				
				int var_id2 = basic_query->getEdgeNeighborID(var_id, i);
				if (var_id2 == -1)
				{
					continue;
				}
				
				//processing_tag = true;//if do not join or select, we do not clear the result list
				Triple curTriple = basic_query->getTriple(edge_id);
				int pre_id = basic_query->getEdgePreID(var_id, i);//predicate id
				char edge_type = basic_query->getEdgeType(var_id, i);
				IDList& can_list = basic_query->getCandidateList(var_id2);
				
				if (!dealed_id_list[var_id2])
				{
					//cout << "join +++ " << var_id << "\t" << var_id2 << "\t" << pre_id << "\t" << dealed_id_list[var_id2] << "\t" << can_list.size() << "\t" << curTriple.getPredicate() << endl;
					//join
					bool shouldVar2AddLiteralCandidateWhenJoin = basic_query->isFreeLiteralVariable(var_id2) && !basic_query->isAddedLiteralCandidate(var_id2);
					
					join(*p_result_list, var_id, pre_id, var_id2, edge_type,
						 var_num, shouldVar2AddLiteralCandidateWhenJoin, can_list, dealed_internal_id_list);
					/*
					ss.str("");
					ss << var_id << "\t" << var_id2 << "\t";
					string output_str = this->printResList(p_result_list, var_num + 1, ss.str());
					log_output << "after join ++++++++ "<< endl << output_str << endl;
					*/
					var_stack.push(var_id2);
					basic_query->setAddedLiteralCandidate(var_id2);
					dealed_id_list[var_id2] = true;
				}
				else
				{
					//cout << "select +++ " << var_id << "\t" << var_id2 << "\t" << pre_id << "\t" << dealed_id_list[var_id2] << "\t" << curTriple.getPredicate() << endl;
					//select
					select(*p_result_list, var_id, pre_id, var_id2, edge_type,var_num);
					//join_tag = true;
					/*
					ss.str("");
					ss << var_id << "\t" << var_id2 << "\t";
					output_str = this->printResList(p_result_list, var_num + 1, ss.str());
					log_output << "after select ======== "<< endl << output_str << endl;
					*/
				}

				dealed_triple[edge_id] = true;
			}
		}
		//cout << "--------------variable " << res_j << " of " << var_num << " finds " << p_result_list->size() << " results" << endl;
		all_result_list->insert(all_result_list->end(), p_result_list->begin(), p_result_list->end());
		dealed_internal_id_list[start_var_id] = true;
	}

    //cout << "OOOOOUT join partial evaluation and find out " << all_result_list->size() << " results" << endl;
	//this->printResList(all_result_list, var_num,  "\t");
	ResListtoString(all_result_list, var_num, res);
	//cout << res << endl;
	
    return true;
}

bool
Database::join_pe_ask(BasicQuery* basic_query, string& res)
{
    int var_num = basic_query->getVarNum();
    int triple_num = basic_query->getTripleNum();

    // initial p_result_list, push min_var_list in
	vector<int*>* all_result_list = &basic_query->getResultList();
	//set<int*>* all_result_set;
	all_result_list->clear();
	//set<string> dealed_subquery;
	
	//ofstream log_output("log_intermedidate.txt");
	bool* dealed_internal_id_list = new bool[var_num];
	memset(dealed_internal_id_list, 0, sizeof(bool) * var_num);
	
	for(int res_j = 0; res_j < var_num; res_j++){
		int var_degree = basic_query->getVarDegree(res_j);
		//bool join_tag = false, processing_tag = false;
		for (int i = 0; i < var_degree; i++)
		{
			// each triple/edge need to be processed only once.
			int edge_id = basic_query->getEdgeID(res_j, i);
			
			int var_id2 = basic_query->getEdgeNeighborID(res_j, i);
			if (var_id2 == -1)
			{
				continue;
			}
			
			//processing_tag = true;//if do not join or select, we do not clear the result list
			Triple curTriple = basic_query->getTriple(edge_id);
			int pre_id = basic_query->getEdgePreID(res_j, i);//predicate id
			
			if(curTriple.getPredicate().at(0) != '?' && pre_id == -1){
				return false;
			}
		}
	}

	for(int res_j = 0; res_j < var_num; res_j++){
	
		//mark dealed_id_list and dealed_triple, 0 not processed, 1 for processed
		bool* dealed_id_list = new bool[var_num];
		bool* dealed_triple = new bool[triple_num];
		memset(dealed_id_list, 0, sizeof(bool) * var_num);
		memset(dealed_triple, 0, sizeof(bool) * triple_num);

		int start_var_id = res_j;
		int start_var_size = basic_query->getCandidateSize(start_var_id);
		//cout << "--------------variable " << res_j << " of " << var_num << " has " << start_var_size << " candidates" << endl;
		
		//start_var_size == 0  no answer in this basic query
		if (start_var_size == 0)
		{
			continue;
		}

		vector<int*>* p_result_list = new vector<int*>;
		IDList* p_min_var_list = &(basic_query->getCandidateList(start_var_id));
		for (int i = 0; i < start_var_size; i++)
		{
			int* result_var = new int[var_num + 1];
			memset(result_var, -1, sizeof(int) * (var_num + 1));
			result_var[start_var_id] = p_min_var_list->getID(i);
			p_result_list->push_back(result_var);
		}
		
		stack<int> var_stack;
		var_stack.push(start_var_id);
		dealed_id_list[start_var_id] = true;
		//log_output << "start_var_id = " << start_var_id << endl;
		//dealed_internal_id_list[start_var_id] = true;
		//dealed_subquery.insert(this->printDealedVertices(dealed_internal_id_list, var_num));
		/*
		for(set<string>::iterator it = dealed_subquery.begin(); it != dealed_subquery.end(); it++){
			cout << *it << ";";
		}
		cout << endl;
		*/
		while (!var_stack.empty())
		{
			int var_id = var_stack.top();
			
			stringstream ss;
			/*
			ss << var_stack.size() << "-" << var_id << "\t";
			string output_str = this->printResList(p_result_list, var_num + 1, ss.str());
			log_output << output_str << endl;
			*/
			var_stack.pop();
			
			int var_degree = basic_query->getVarDegree(var_id);
			//bool join_tag = false, processing_tag = false;
			for (int i = 0; i < var_degree; i++)
			{
				// each triple/edge need to be processed only once.
				int edge_id = basic_query->getEdgeID(var_id, i);
				if (dealed_triple[edge_id])
				{
					continue;
				}
				
				int var_id2 = basic_query->getEdgeNeighborID(var_id, i);
				if (var_id2 == -1)
				{
					continue;
				}
				
				//processing_tag = true;//if do not join or select, we do not clear the result list
				Triple curTriple = basic_query->getTriple(edge_id);
				int pre_id = basic_query->getEdgePreID(var_id, i);//predicate id
				char edge_type = basic_query->getEdgeType(var_id, i);
				IDList& can_list = basic_query->getCandidateList(var_id2);
				
				if (!dealed_id_list[var_id2])
				{
					//cout << "join +++" << var_id << "\t" << var_id2 << "\t" << pre_id << "\t" << dealed_id_list[var_id2] << "\t" << curTriple.getPredicate() << endl;
					//join
					join(*p_result_list, var_id, pre_id, var_id2, edge_type,
						 var_num, true, can_list, dealed_internal_id_list);
					/*
					ss.str("");
					ss << var_id << "\t" << var_id2 << "\t";
					string output_str = this->printResList(p_result_list, var_num + 1, ss.str());
					log_output << "after join ++++++++ "<< endl << output_str << endl;
					*/
					var_stack.push(var_id2);
					basic_query->setAddedLiteralCandidate(var_id2);
					dealed_id_list[var_id2] = true;
				}
				else
				{
					//cout << "select +++" << var_id << "\t" << var_id2 << "\t" << pre_id << "\t" << dealed_id_list[var_id2] << "\t" << curTriple.getPredicate() << endl;
					//select
					select(*p_result_list, var_id, pre_id, var_id2, edge_type,var_num);
					//join_tag = true;
					/*
					ss.str("");
					ss << var_id << "\t" << var_id2 << "\t";
					output_str = this->printResList(p_result_list, var_num + 1, ss.str());
					log_output << "after select ======== "<< endl << output_str << endl;
					*/
				}

				dealed_triple[edge_id] = true;
			}
		}
		//cout << "--------------variable " << res_j << " of " << var_num << " finds " << p_result_list->size() << " results" << endl;
		all_result_list->insert(all_result_list->end(), p_result_list->begin(), p_result_list->end());
		dealed_internal_id_list[start_var_id] = true;
	}

	//ResListtoString(all_result_list, var_num, res);
	char* dealed_internal_id_sign = new char[var_num + 1];
	set<string> LECF_set;
	
	//ofstream log_output("log_ask_intermedidate.txt");
	for(vector<int*>::iterator it = all_result_list->begin(); it != all_result_list->end(); it++){
		memset(dealed_internal_id_sign, '0', sizeof(bool) * var_num);
		dealed_internal_id_sign[var_num] = 0;
		int* result_var = *it;		
		stringstream res_ss;
		
		for(int j = 0; j < var_num; j++){
			int var_degree = basic_query->getVarDegree(j);
			/*
			if(result_var[j] != -1){
				string _tmp_1;
				if(result_var[j] < LITERAL_FIRST_ID){
					_tmp_1 = (this->kvstore)->getEntityByID(result_var[j]);
				}else{
					_tmp_1 = (this->kvstore)->getLiteralByID(result_var[j]);
				}
				log_output << _tmp_1 << "\t";
			}else{
				log_output << result_var[j] << "\t";
			}
			*/
			if(result_var[j] == -1)
				continue;
			
			for (int i = 0; i < var_degree; i++)
			{
				if(result_var[j] >= LITERAL_FIRST_ID || this->internal_tag_arr.at(result_var[j]) == '1'){
					dealed_internal_id_sign[j] = '1';
				}else{
					continue;
				}
				// each triple/edge need to be processed only once.
				int edge_id = basic_query->getEdgeID(j, i);				
				int var_id2 = basic_query->getEdgeNeighborID(j, i);
				if (var_id2 == -1)
				{
					continue;
				}
				
				if(result_var[var_id2] != -1){
					if(result_var[var_id2] < LITERAL_FIRST_ID && this->internal_tag_arr.at(result_var[var_id2]) == '0'){
						string _tmp_1, _tmp_2;
						if(result_var[j] < LITERAL_FIRST_ID){
							_tmp_1 = (this->kvstore)->getEntityByID(result_var[j]);
						}else{
							_tmp_1 = (this->kvstore)->getLiteralByID(result_var[j]);
						}
						
						if(result_var[var_id2] < LITERAL_FIRST_ID){
							_tmp_2 = (this->kvstore)->getEntityByID(result_var[var_id2]);
						}else{
							_tmp_2 = (this->kvstore)->getLiteralByID(result_var[var_id2]);
						}
						if(j < var_id2){
							res_ss << j << "\t" << var_id2 << "\t" << _tmp_1 << "\t" << _tmp_2 << "\t";
						}else{
							res_ss << var_id2 << "\t" << j << "\t" << _tmp_2 << "\t" << _tmp_1 << "\t";
						}
					}
				}
			}
		}
		res_ss << dealed_internal_id_sign << endl;
		//log_output << res_ss.str() << endl;
		LECF_set.insert(res_ss.str());
	}
	delete[] dealed_internal_id_sign;
	
	stringstream all_res_ss;
	for(set<string>::iterator it1 = LECF_set.begin(); it1 != LECF_set.end(); it1++){
		all_res_ss << *it1 << endl;
	}
	res = all_res_ss.str();
	
    return true;
}

bool
Database::join_basic(BasicQuery* basic_query)
{
    cout << "IIIIIIN join basic" << endl;

    int var_num = basic_query->getVarNum();
    int triple_num = basic_query->getTripleNum();

    //mark dealed_id_list and dealed_triple, 0 not processed, 1 for processed
    bool* dealed_id_list = new bool[var_num];
    bool* dealed_triple = new bool[triple_num];
    memset(dealed_id_list, 0, sizeof(bool) * var_num);
    memset(dealed_triple, 0, sizeof(bool) * triple_num);

    int start_var_id = basic_query->getVarID_FirstProcessWhenJoin();
    int start_var_size = basic_query->getCandidateSize(start_var_id);

    // initial p_result_list, push min_var_list in
    vector<int*>* p_result_list = &basic_query->getResultList();
    p_result_list->clear();

    //start_var_size == 0  no answer in this basic query
    if (start_var_size == 0)
    {
        return false;
    }

    //debug
    {
        stringstream _ss;
        _ss << "start_var_size=" << start_var_size << endl;
        _ss << "star_var=" << basic_query->getVarName(start_var_id) << "(var[" << start_var_id << "])" << endl;
        Database::log(_ss.str());
    }

    IDList* p_min_var_list = &(basic_query->getCandidateList(start_var_id));
    for (int i = 0; i < start_var_size; i++)
    {
        int* result_var = new int[var_num + 1];
        memset(result_var, -1, sizeof(int) * (var_num + 1));
        result_var[start_var_id] = p_min_var_list->getID(i);
        p_result_list->push_back(result_var);
    }

    //BFS search

    stack<int> var_stack;
    var_stack.push(start_var_id);
    dealed_id_list[start_var_id] = true;
    while (!var_stack.empty())
    {
        int var_id = var_stack.top();
        var_stack.pop();
        int var_degree = basic_query->getVarDegree(var_id);
        for (int i = 0; i < var_degree; i++)
        {
            // each triple/edge need to be processed only once.
            int edge_id = basic_query->getEdgeID(var_id, i);
            if (dealed_triple[edge_id])
            {
                continue;
            }
            int var_id2 = basic_query->getEdgeNeighborID(var_id, i);
            if (var_id2 == -1)
            {
                continue;
            }

            int pre_id = basic_query->getEdgePreID(var_id, i);
            char edge_type = basic_query->getEdgeType(var_id, i);
            IDList& can_list = basic_query->getCandidateList(var_id2);

            if (!dealed_id_list[var_id2])
            {
                //join
                bool shouldVar2AddLiteralCandidateWhenJoin = basic_query->isFreeLiteralVariable(var_id2) &&
                        !basic_query->isAddedLiteralCandidate(var_id2);

                join(*p_result_list, var_id, pre_id, var_id2, edge_type,
                     var_num, shouldVar2AddLiteralCandidateWhenJoin, can_list);
                var_stack.push(var_id2);
                basic_query->setAddedLiteralCandidate(var_id2);
                dealed_id_list[var_id2] = true;
            }
            else
            {
                //select
                select(*p_result_list, var_id, pre_id, var_id2, edge_type,var_num);
            }

            dealed_triple[edge_id] = true;
        }
    }

    cout << "OOOOOUT join basic" << endl;
    return true;
}

void
Database::filter_before_join(BasicQuery* basic_query)
{

    //cout << "*****IIIIIIN filter_before_join" << endl;

    int var_num = 0;
    var_num = basic_query->getVarNum();

    for (int i = 0; i < var_num; i++)
    {
        //cout << "\tVar" << i << " " << basic_query->getVarName(i) << endl;
        IDList &can_list = basic_query->getCandidateList(i);
        //cout << "\t\tsize of canlist before filter: " << can_list.size() << endl;
        // must sort before using binary search.
        can_list.sort();

        long begin = Util::get_cur_time();
        this->literal_edge_filter(basic_query, i);
        long after_literal_edge_filter = Util::get_cur_time();
        //cout << "\t\tliteral_edge_filter: used " << (after_literal_edge_filter-begin) << " ms" << endl;
//		this->preid_filter(basic_query, i);
//		long after_preid_filter = Util::get_cur_time();
//		cout << "\t\tafter_preid_filter: used " << (after_preid_filter-after_literal_edge_filter) << " ms" << endl;
        //cout << "\t\t[" << i << "] after filter, candidate size = " << can_list.size() << endl << endl << endl;

        //debug
//		{
//			stringstream _ss;
//			for(int i = 0; i < can_list.size(); i ++)
//			{
//				string _can = this->kvstore->getEntityByID(can_list[i]);
//				_ss << "[" << _can << ", " << can_list[i] << "]\t";
//			}
//			_ss << endl;
//			Database::log(_ss.str());
//			cout << can_list.to_str() << endl;
//		}
    }

    //cout << "OOOOOOUT filter_before_join" << endl;
}

void
Database::literal_edge_filter(BasicQuery* basic_query, int _var_i)
{
    Database::log("IN literal_edge_filter"); //debug

    int var_degree = basic_query->getVarDegree(_var_i);
    for(int j = 0; j < var_degree; j ++)
    {
        int neighbor_id = basic_query->getEdgeNeighborID(_var_i, j);
        //	continue;
        //cout << "\t\t\tneighbor_id=" << neighbor_id << endl;
        if (neighbor_id != -1)
        {
            continue;
        }

        char edge_type = basic_query->getEdgeType(_var_i, j);
        int triple_id = basic_query->getEdgeID(_var_i, j);
        Triple triple = basic_query->getTriple(triple_id);
        string neighbor_name;

        if (edge_type == BasicQuery::EDGE_OUT)
        {
            neighbor_name = triple.object;
        }
        else
        {
            neighbor_name = triple.subject;
        }

        /* if neightbor is an var, but not in select
         * then, if its degree is 1, it has none contribution to filter
         * only its sole edge property(predicate) makes sense
         * we should make sure that current candidateVar has an edge matching the predicate
         *  */
        bool only_preid_filter = (basic_query->isOneDegreeNotSelectVar(neighbor_name));
        if(only_preid_filter)
        {
            continue;
        }

        int pre_id = basic_query->getEdgePreID(_var_i, j);
        IDList &_list = basic_query->getCandidateList(_var_i);

        int lit_id = (this->kvstore)->getIDByEntity(neighbor_name);
        if(lit_id == -1)
        {
            lit_id = (this->kvstore)->getIDByLiteral(neighbor_name);
        }

        //			cout << "\t\tedge[" << j << "] "<< lit_string << " has id " << lit_id << "";
        //			cout << " preid:" << pre_id << " type:" << edge_type
        //					<< endl;
//		{
//					stringstream _ss;
//					_ss << "\t\tedge[" << j << "] "<< lit_string << " has id " << lit_id << "";
//					_ss << " preid:" << pre_id << " type:" << edge_type
//							<< endl;
//					Database::log(_ss.str());
//		}

        int id_list_len = 0;
        int* id_list = NULL;
        if (pre_id >= 0)
        {
            if (edge_type == BasicQuery::EDGE_OUT)
            {
                (this->kvstore)->getsubIDlistByobjIDpreID(lit_id, pre_id, id_list, id_list_len);
            }
            else
            {
                (this->kvstore)->getobjIDlistBysubIDpreID(lit_id, pre_id, id_list, id_list_len);
            }
        }
        else
            /* pre_id == -1 means we cannot find such predicate in rdf file, so the result set of this sparql should be empty.
             * note that we cannot support to query sparqls with predicate variables ?p.
             */
        {
            id_list_len = 0;
//			if (edge_type == BasicQuery::EDGE_OUT)
//			{
//			    (this->kvstore)->getsubIDlistByobjID(lit_id, id_list, id_list_len);
//			}
//			else
//			{
//			    (this->kvstore)->getobjIDlistBysubID(lit_id, id_list, id_list_len);
//			}
        }

        //debug
        //      {
        //          stringstream _ss;
        //          _ss << "id_list: ";
        //          for (int i=0;i<id_list_len;i++)
        //          {
        //              _ss << "[" << id_list[i] << "]\t";
        //          }
        //          _ss<<endl;
        //          Database::log(_ss.str());
        //      }

        if(id_list_len == 0)
        {
            _list.clear();
            delete []id_list;
            return;
        }
        //			cout << "\t\t can:" << can_list.to_str() << endl;
        //			cout << "\t\t idlist has :";
        //			for(int i_ = 0; i_ < id_list_len; i_ ++)
        //			{
        //				cout << "[" << id_list[i_] << "]\t";
        //			}
        //			cout << endl;

        _list.intersectList(id_list, id_list_len);
        delete []id_list;
    }

    Database::log("OUT literal_edge_filter"); //debug
}

/* this part can be omited or improved if the encode way of predicate
 * is good enough
 * also, we can decide whether we need run this part (if there are predicates encode overlap)
 * by var_i's edge in queryGraph,
 *
 *
 * for each edge e of var_i,
 * if neightbor on e is an var, but not in select
 * then, if the var's degree is 1, it has none contribution to filter
 * only its sole edge property(predicate) makes sense
 * we should make sure that var_i has an edge matching the predicate
 * so this function will do the filtering
 * TBD:
 * if pre_id = -1,
 * it means the entity id must has at least one  edge*/
void
Database::preid_filter(BasicQuery* basic_query, int _var_i)
{
    //IDList & _list, int _pre_id, char _edge_type
    for (int j = 0; j < basic_query->getVarDegree(_var_i); j++)
    {
        int neighbor_id = basic_query->getEdgeNeighborID(_var_i, j);
        //	continue;
        //cout << "\t\t\tneighbor_id=" << neighbor_id << endl;
        if (neighbor_id != -1)
        {
            continue;
        }

        char edge_type = basic_query->getEdgeType(_var_i, j);
        int triple_id = basic_query->getEdgeID(_var_i, j);
        Triple triple = basic_query->getTriple(triple_id);
        string neighbor_name;

        if (edge_type == BasicQuery::EDGE_OUT)
        {
            neighbor_name = triple.object;
        }
        else
        {
            neighbor_name = triple.subject;
        }

        /* if neightbor is an var, but not in select
         * then, if its degree is 1, it has none contribution to filter
         * only its sole edge property(predicate) makes sense
         * we should make sure that current candidateVar has an edge matching the predicate
         *  */
        bool only_preid_filter = (basic_query->isOneDegreeNotSelectVar(neighbor_name));
        if (!only_preid_filter)
        {
            continue;
        }

        int pre_id = basic_query->getEdgePreID(_var_i, j);
        IDList& _list = basic_query->getCandidateList(_var_i);
        int* remain_list = new int[_list.size()];
        int remain_len = 0;
        int _entity_id = -1;
        int* pair_list = NULL;
        int pair_len = 0;

        for (int i = 0; i < _list.size(); i++)
        {
            _entity_id = _list[i];
            if (edge_type == BasicQuery::EDGE_IN)
            {
                (this->kvstore)->getpreIDsubIDlistByobjID
                (_entity_id, pair_list,	pair_len);
            }
            else
            {
                (this->kvstore)->getpreIDobjIDlistBysubID
                (_entity_id, pair_list,	pair_len);
            }

            bool exist_preid = Util::bsearch_preid_uporder
                               (pre_id, pair_list,	pair_len);

            if (exist_preid)
            {
                remain_list[remain_len] = _entity_id;
                remain_len++;
            }

            delete[] pair_list;
            pair_len = 0;
        }/* end for i 0 to _list.size */

        _list.intersectList(remain_list, remain_len);

        /* can be imported */
        delete[] remain_list;
    }/* end for j : varDegree */
}

void
Database::only_pre_filter_after_join(BasicQuery* basic_query)
{
    int var_num = basic_query->getVarNum();
    vector<int*>& result_list = basic_query->getResultList();

    for (int var_id = 0; var_id < var_num; var_id++)
    {
        int var_degree = basic_query->getVarDegree(var_id);

        // get all the only predicate filter edges for this variable.
        vector<int> in_edge_pre_id;
        vector<int> out_edge_pre_id;
        for (int i = 0; i < var_degree; i++)
        {
            char edge_type = basic_query->getEdgeType(var_id, i);
            int triple_id = basic_query->getEdgeID(var_id, i);
            Triple triple = basic_query->getTriple(triple_id);
            string neighbor_name;

            if (edge_type == BasicQuery::EDGE_OUT)
            {
                neighbor_name = triple.object;
            }
            else
            {
                neighbor_name = triple.subject;
            }

            bool only_preid_filter = (basic_query->isOneDegreeNotSelectVar(neighbor_name));
            if (!only_preid_filter)
            {
                continue;
            }

            int pre_id = basic_query->getEdgePreID(var_id, i);

            if (edge_type == BasicQuery::EDGE_OUT)
            {
                out_edge_pre_id.push_back(pre_id);
            }
            else
            {
                in_edge_pre_id.push_back(pre_id);
            }
        }

        if (in_edge_pre_id.empty() && out_edge_pre_id.empty())
        {
            continue;
        }

        for (vector<int*>::iterator itr = result_list.begin(); itr != result_list.end(); itr++)
        {
            int* res_seq = (*itr);
            if (res_seq[var_num] == -1)
            {
                continue;
            }

            int entity_id = res_seq[var_id];
            int* pair_list = NULL;
            int pair_len = 0;
            bool exist_preid = true;

            if (exist_preid && !in_edge_pre_id.empty())
            {
                (this->kvstore)->getpreIDsubIDlistByobjID(entity_id, pair_list, pair_len);

                for (vector<int>::iterator itr_pre = in_edge_pre_id.begin(); itr_pre != in_edge_pre_id.end(); itr_pre++)
                {
                    int pre_id = (*itr_pre);
                    exist_preid = Util::bsearch_preid_uporder(pre_id, pair_list, pair_len);
                    if (!exist_preid)
                    {
                        break;
                    }
                }
                delete []pair_list;
            }
            if (exist_preid && !out_edge_pre_id.empty())
            {
                (this->kvstore)->getpreIDobjIDlistBysubID(entity_id, pair_list, pair_len);

                for (vector<int>::iterator itr_pre = out_edge_pre_id.begin(); itr_pre != out_edge_pre_id.end(); itr_pre++)
                {
                    int pre_id = (*itr_pre);
                    exist_preid = Util::bsearch_preid_uporder(pre_id, pair_list, pair_len);
                    if (!exist_preid)
                    {
                        break;
                    }
                }
                delete []pair_list;
            }

            // result sequence is illegal when there exists any missing filter predicate id.
            if (!exist_preid)
            {
                res_seq[var_num] = -1;
            }
        }
    }
}

/* add literal candidates to these variables' candidate list which may include literal results. */
void
Database::add_literal_candidate(BasicQuery* basic_query)
{
    Database::log("IN add_literal_candidate");

    int var_num = basic_query->getVarNum();

    // deal with literal variable candidate list.
    // because we do not insert any literal elements into VSTree, we can not retrieve them from VSTree.
    // for these variable which may include some literal results, we should add all possible literal candidates to the candidate list.
    for (int i = 0; i < var_num; i++)
    {
        //debug
        {
            stringstream _ss;
            _ss << "var[" << i << "]\t";
            if (basic_query->isLiteralVariable(i))
            {
                _ss << "may have literal result.";
            }
            else
            {
                _ss << "do not have literal result.";
            }
            _ss << endl;
            Database::log(_ss.str());
        }

        if (!basic_query->isLiteralVariable(i))
        {
            // if this variable is not literal variable, we can assume that its literal candidates have been added.
            basic_query->setAddedLiteralCandidate(i);
            continue;
        }

        // for these literal variable without any linking entities(we call free literal variable),
        // we will add their literal candidates when join-step.
        if (basic_query->isFreeLiteralVariable(i))
        {
            continue;
        }


        int var_id = i;
        int var_degree = basic_query->getVarDegree(var_id);
        IDList literal_candidate_list;

        // intersect each edge's literal candidate.
        for (int j = 0; j < var_degree; j ++)
        {
            int neighbor_id = basic_query->getEdgeNeighborID(var_id, j);
            int predicate_id = basic_query->getEdgePreID(var_id, j);
            int triple_id = basic_query->getEdgeID(var_id, j);
            Triple triple = basic_query->getTriple(triple_id);
            string neighbor_name = triple.subject;
            IDList this_edge_literal_list;

            // if the neighbor of this edge is an entity, we can add all literals which has an exact predicate edge linking to this entity.
            if (neighbor_id == -1)
            {
                int subject_id = (this->kvstore)->getIDByEntity(neighbor_name);
                int* object_list = NULL;
                int object_list_len = 0;

                (this->kvstore)->getobjIDlistBysubIDpreID(subject_id, predicate_id, object_list, object_list_len);
                this_edge_literal_list.unionList(object_list, object_list_len);
                delete []object_list;
            }
            // if the neighbor of this edge is variable, then the neighbor variable can not have any literal results,
            // we should add literals when join these two variables, see the Database::join function for details.

            // deprecated...
            // if the neighbor of this edge is variable, we should add all this neighbor variable's candidate entities' neighbor literal,
            // which has one corresponding predicate edge linking to this variable.
            else
            {

                /*
                IDList& neighbor_candidate_list = basic_query->getCandidateList(neighbor_id);
                int neighbor_candidate_list_size = neighbor_candidate_list.size();
                for (int k = 0;k < neighbor_candidate_list_size; k ++)
                {
                    int subject_id = neighbor_candidate_list.getID(k);
                    int* object_list = NULL;
                    int object_list_len = 0;

                    (this->kvstore)->getobjIDlistBysubIDpreID(subject_id, predicate_id, object_list, object_list_len);
                    this_edge_literal_list.unionList(object_list, object_list_len);
                    delete []object_list;
                }
                */
            }


            if (j == 0)
            {
                literal_candidate_list.unionList(this_edge_literal_list);
            }
            else
            {
                literal_candidate_list.intersectList(this_edge_literal_list);
            }
        }

        // add the literal_candidate_list to the original candidate list.
        IDList& origin_candidate_list = basic_query->getCandidateList(var_id);
        int origin_candidate_list_len = origin_candidate_list.size();
        origin_candidate_list.unionList(literal_candidate_list);
        int after_add_literal_candidate_list_len = origin_candidate_list.size();

        // this variable's literal candidates have been added.
        basic_query->setAddedLiteralCandidate(var_id);

        //debug
        {
            stringstream _ss;
            _ss << "var[" << var_id << "] candidate list after add literal:\t"
                << origin_candidate_list_len << "-->" << after_add_literal_candidate_list_len << endl;
            /*
            for (int i = 0; i < after_add_literal_candidate_list_len; i ++)
            {
                int candidate_id = origin_candidate_list.getID(i);
                string candidate_name;
                if (i < origin_candidate_list_len)
                {
                    candidate_name = (this->kvstore)->getEntityByID(origin_candidate_list.getID(i));
                }
                else
                {
                    candidate_name = (this->kvstore)->getLiteralByID(origin_candidate_list.getID(i));
                }
                _ss << candidate_name << "(" << candidate_id << ")\t";
            }
            */
            Database::log(_ss.str());
        }
    }

    Database::log("OUT add_literal_candidate");
}

//get the final string result_set from SPARQLquery
bool
Database::getFinalResult(SPARQLquery& _sparql_q, ResultSet& _result_set)
{
#ifdef DEBUG_PRECISE
	printf("getFinalResult:begins\n");
#endif
    int _var_num = _sparql_q.getQueryVarNum();
    _result_set.setVar(_sparql_q.getQueryVar());
    vector<BasicQuery*>& query_vec = _sparql_q.getBasicQueryVec();

    //sum the answer number
    int _ans_num = 0;
#ifdef DEBUG_PRECISE
	printf("getFinalResult:before ansnum loop\n");
#endif
    for(unsigned i = 0; i < query_vec.size(); i ++)
    {
        _ans_num += query_vec[i]->getResultList().size();
    }
#ifdef DEBUG_PRECISE
	printf("getFinalResult:after ansnum loop\n");
#endif

    _result_set.ansNum = _ans_num;
#ifndef STREAM_ON
    _result_set.answer = new string*[_ans_num];
    for(int i = 0; i < _result_set.ansNum; i ++)
    {
        _result_set.answer[i] = NULL;
    }
#else
    _result_set.openStream();
#ifdef DEBUG_PRECISE
	printf("getFinalResult:after open stream\n");
#endif
#endif
#ifdef DEBUG_PRECISE
	printf("getFinalResult:before main loop\n");
#endif
    int tmp_ans_count = 0;
    //map int ans into string ans
    //union every basic result into total result
    for(unsigned i = 0; i < query_vec.size(); i++)
    {
        vector<int*>& tmp_vec = query_vec[i]->getResultList();
        vector<int*>::iterator itr = tmp_vec.begin();
        //for every result group in resultlist
        for(; itr != tmp_vec.end(); itr++)
        {
#ifndef STREAM_ON
            _result_set.answer[tmp_ans_count] = new string[_var_num];
#endif
#ifdef DEBUG_PRECISE
	printf("getFinalResult:before map loop\n");
#endif
            //map every ans_id into ans_str
            for(int v = 0; v < _var_num; v++)
            {
                int ans_id = (*itr)[v];
                string ans_str;
				if(ans_id == -1){
					ans_str = "-1";
				}else if (this->objIDIsEntityID(ans_id))
                {
                    ans_str = (this->kvstore)->getEntityByID(ans_id);
                }
                else
                {
                    ans_str = (this->kvstore)->getLiteralByID(ans_id);
                }
#ifndef STREAM_ON
                _result_set.answer[tmp_ans_count][v] = ans_str;
#else
                _result_set.writeToStream(ans_str);
#endif
#ifdef DEBUG_PRECISE
	printf("getFinalResult:after copy/write\n");
#endif
            }
            tmp_ans_count++;
        }
    }
#ifdef STREAM_ON
    _result_set.resetStream();
#endif
#ifdef DEBUG_PRECISE
	printf("getFinalResult:ends\n");
#endif

    return true;
}


//FILE* Database::fp_debug = NULL;

void
Database::log(string _str)
{
    _str += "\n";
#ifdef DEBUG_DATABASE
    fputs(_str.c_str(), Util::debug_database);
    fflush(Util::debug_database);
#endif

#ifdef DEBUG_VSTREE
    fputs(_str.c_str(), Util::debug_database);
    fflush(Util::debug_database);
#endif
}

void
Database::printIDlist(int _i, int* _list, int _len, string _log)
{
    stringstream _ss;
    _ss << "[" << _i << "] ";
    for(int i = 0; i < _len; i ++) {
        _ss << _list[i] << "\t";
    }
    Database::log("=="+_log + ":");
    Database::log(_ss.str());
}

void
Database::printPairList(int _i, int* _list, int _len, string _log)
{
    stringstream _ss;
    _ss << "[" << _i << "] ";
    for(int i = 0; i < _len; i += 2) {
        _ss << "[" << _list[i] << "," << _list[i+1] << "]\t";
    }
    Database::log("=="+_log + ":");
    Database::log(_ss.str());
}

void
Database::test()
{
    int subNum = 9, preNum = 20, objNum = 90;

    int* _id_list = NULL;
    int _list_len = 0;
    {   /* x2ylist */
        for (int i = 0; i < subNum; i++)
        {

            (this->kvstore)->getobjIDlistBysubID(i, _id_list, _list_len);
            if (_list_len != 0)
            {
                stringstream _ss;
                this->printIDlist(i, _id_list, _list_len, "s2olist["+_ss.str()+"]");
                delete[] _id_list;
            }

            /* o2slist */
            (this->kvstore)->getsubIDlistByobjID(i, _id_list, _list_len);
            if (_list_len != 0)
            {
                stringstream _ss;
                this->printIDlist(i, _id_list, _list_len, "o(sub)2slist["+_ss.str()+"]");
                delete[] _id_list;
            }
        }

        for (int i = 0; i < objNum; i++)
        {
            int _i = Database::LITERAL_FIRST_ID + i;
            (this->kvstore)->getsubIDlistByobjID(_i, _id_list, _list_len);
            if (_list_len != 0)
            {
                stringstream _ss;
                this->printIDlist(_i, _id_list, _list_len, "o(literal)2slist["+_ss.str()+"]");
                delete[] _id_list;
            }
        }
    }
    {   /* xy2zlist */
        for (int i = 0; i < subNum; i++)
        {
            for (int j = 0; j < preNum; j++)
            {
                (this->kvstore)->getobjIDlistBysubIDpreID(i, j, _id_list,
                        _list_len);
                if (_list_len != 0)
                {
                    stringstream _ss;
                    _ss << "preid:" << j ;
                    this->printIDlist(i, _id_list, _list_len, "sp2olist["+_ss.str()+"]");
                    delete[] _id_list;
                }

                (this->kvstore)->getsubIDlistByobjIDpreID(i, j, _id_list,
                        _list_len);
                if (_list_len != 0)
                {
                    stringstream _ss;
                    _ss << "preid:" << j ;
                    this->printIDlist(i, _id_list, _list_len, "o(sub)p2slist["+_ss.str()+"]");
                    delete[] _id_list;
                }
            }
        }

        for (int i = 0; i < objNum; i++)
        {
            int _i = Database::LITERAL_FIRST_ID + i;
            for (int j = 0; j < preNum; j++)
            {
                (this->kvstore)->getsubIDlistByobjIDpreID(_i, j, _id_list,
                        _list_len);
                if (_list_len != 0)
                {
                    stringstream _ss;
                    _ss << "preid:" << j ;
                    this->printIDlist(_i, _id_list, _list_len,
                                      "*o(literal)p2slist["+_ss.str()+"]");
                    delete[] _id_list;
                }
            }
        }
    }
    {   /* x2yzlist */
        for (int i = 0; i < subNum; i++)
        {
            (this->kvstore)->getpreIDobjIDlistBysubID(i, _id_list, _list_len);
            if (_list_len != 0)
            {
                this->printPairList(i, _id_list, _list_len, "s2polist");
                delete[] _id_list;
                _list_len = 0;
            }
        }

        for (int i = 0; i < subNum; i++)
        {
            (this->kvstore)->getpreIDsubIDlistByobjID(i, _id_list, _list_len);
            if (_list_len != 0)
            {
                this->printPairList(i, _id_list, _list_len, "o(sub)2pslist");
                delete[] _id_list;
            }
        }

        for (int i = 0; i < objNum; i++)
        {
            int _i = Database::LITERAL_FIRST_ID + i;
            (this->kvstore)->getpreIDsubIDlistByobjID(_i, _id_list, _list_len);
            if (_list_len != 0)
            {
                this->printPairList(_i, _id_list, _list_len,
                                    "o(literal)2pslist");
                delete[] _id_list;
            }
        }
    }
}

void
Database::test_build_sig()
{
    BasicQuery* _bq = new BasicQuery("");
    /*
     * <!!!>	y:created	<!!!_(album)>.
     *  <!!!>	y:created	<Louden_Up_Now>.
     *  <!!!_(album)>	y:hasSuccessor	<Louden_Up_Now>
     * <!!!_(album)>	rdf:type	<wordnet_album_106591815>
     *
     * id of <!!!> is 0
     * id of <!!!_(album)> is 2
     *
     *
     * ?x1	y:created	?x2.
     *  ?x1	y:created	<Louden_Up_Now>.
     *  ?x2	y:hasSuccessor	<Louden_Up_Now>.
     * ?x2	rdf:type	<wordnet_album_106591815>
     */
    {
        Triple _triple("?x1", "y:created", "?x2");
        _bq->addTriple(_triple);
    }
    {
        Triple _triple("?x1", "y:created", "<Louden_Up_Now>");
        _bq->addTriple(_triple);
    }
    {
        Triple _triple("?x2", "y:hasSuccessor", "<Louden_Up_Now>");
        _bq->addTriple(_triple);
    }
    {
        Triple _triple("?x2", "rdf:type", "<wordnet_album_106591815>");
        _bq->addTriple(_triple);
    }
    vector<string> _v;
    _v.push_back("?x1");
    _v.push_back("?x2");

    _bq->encodeBasicQuery(this->kvstore, _v);
    Database::log(_bq->to_str());
    SPARQLquery _q;
    _q.addBasicQuery(_bq);

    (this->vstree)->retrieve(_q);

    Database::log("\n\n");
    Database::log("candidate:\n\n"+_q.candidate_str());
}

void
Database::test_join()
{
    BasicQuery* _bq = new BasicQuery("");
    /*
     * <!!!>	y:created	<!!!_(album)>.
     *  <!!!>	y:created	<Louden_Up_Now>.
     *  <!!!_(album)>	y:hasSuccessor	<Louden_Up_Now>
     * <!!!_(album)>	rdf:type	<wordnet_album_106591815>
     *
     * id of <!!!> is 0
     * id of <!!!_(album)> is 2
     *
     *
     * ?x1	y:created	?x2.
     *  ?x1	y:created	<Louden_Up_Now>.
     *  ?x2	y:hasSuccessor	<Louden_Up_Now>.
     * ?x2	rdf:type	<wordnet_album_106591815>
     */
    {
        Triple _triple("?x1", "y:created", "?x2");
        _bq->addTriple(_triple);
    }
    {
        Triple _triple("?x1", "y:created", "<Louden_Up_Now>");
        _bq->addTriple(_triple);
    }
    {
        Triple _triple("?x2", "y:hasSuccessor", "<Louden_Up_Now>");
        _bq->addTriple(_triple);
    }
    {
        Triple _triple("?x2", "rdf:type", "<wordnet_album_106591815>");
        _bq->addTriple(_triple);
    }
    vector<string> _v;
    _v.push_back("?x1");
    _v.push_back("?x2");

    _bq->encodeBasicQuery(this->kvstore, _v);
    Database::log(_bq->to_str());
    SPARQLquery _q;
    _q.addBasicQuery(_bq);

    (this->vstree)->retrieve(_q);

    Database::log("\n\n");
    Database::log("candidate:\n\n"+_q.candidate_str());
    _q.print(cout);

    this->join(_q);
    ResultSet _rs;
    this->getFinalResult(_q, _rs);
    cout << _rs.to_str() << endl;
}

bool
Database::query(const string _query, ResultSet& _result_set, FILE* _fp)
{
    long tv_begin = Util::get_cur_time();

    DBparser _parser;
    SPARQLquery _sparql_q(_query);
    _parser.sparqlParser(_query, _sparql_q);

    long tv_parse = Util::get_cur_time();
    cout << "after Parsing, used " << (tv_parse - tv_begin) << endl;
    cout << "after Parsing..." << endl << _sparql_q.triple_str() << endl;

    _sparql_q.encodeQuery(this->kvstore);

    cout << "sparqlSTR:\t" << _sparql_q.to_str() << endl;

    long tv_encode = Util::get_cur_time();
    cout << "after Encode, used " << (tv_encode - tv_parse) << "ms." << endl;

    _result_set.select_var_num = _sparql_q.getQueryVarNum();

    (this->vstree)->retrieve(_sparql_q);

    long tv_retrieve = Util::get_cur_time();
    cout << "after Retrieve, used " << (tv_retrieve - tv_encode) << "ms." << endl;

    this->join(_sparql_q);

    long tv_join = Util::get_cur_time();
    cout << "after Join, used " << (tv_join - tv_retrieve) << "ms." << endl;

    this->getFinalResult(_sparql_q, _result_set);

    long tv_final = Util::get_cur_time();
    cout << "after finalResult, used " << (tv_final - tv_join) << "ms." << endl;

    cout << "Total time used: " << (tv_final - tv_begin) << "ms." << endl;

    //testing...
    cout << "final result is : " << endl;
#ifndef STREAM_ON
    cout << _result_set.to_str() << endl;
#else
    _result_set.output(_fp);
    //cout<<endl;		//empty the buffer;print an empty line
	fprintf(_fp, "\n");
	fflush(_fp);       //to empty the output buffer in C (fflush(stdin) not work in GCC)
#endif

    return true;
}

//join on the vector of CandidateList, available after retrieved from the VSTREE
//and store the resut in _result_set
bool
Database::join(SPARQLquery& _sparql_query)
{
    int basic_query_num = _sparql_query.getBasicQueryNum();
    //join each basic query
    for(int i=0; i < basic_query_num; i++)
    {
        //cout<<"Basic query "<<i<<endl;
        BasicQuery* basic_query;
        basic_query=&(_sparql_query.getBasicQuery(i));
        long begin = Util::get_cur_time();
        this->filter_before_join(basic_query);
        long after_filter = Util::get_cur_time();
        cout << "after filter_before_join: used " << (after_filter-begin) << " ms" << endl;
        this->add_literal_candidate(basic_query);
        long after_add_literal = Util::get_cur_time();
        cout << "after add_literal_candidate: used " << (after_add_literal-after_filter) << " ms" << endl;
        this->join_basic(basic_query);
        long after_joinbasic = Util::get_cur_time();
        cout << "after join_basic : used " << (after_joinbasic-after_add_literal) << " ms" << endl;
        this->only_pre_filter_after_join(basic_query);
        long after_pre_filter_after_join = Util::get_cur_time();
        cout << "after only_pre_filter_after_join : used " << (after_pre_filter_after_join-after_joinbasic) << " ms" << endl;

        // remove invalid and duplicate result at the end.
        basic_query->dupRemoval_invalidRemoval();
        cout << "Final result:" << (basic_query->getResultList()).size() << endl;
    }
    return true;
}
