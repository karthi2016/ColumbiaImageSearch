#include "hasher_obj.hpp"
#include "iotools.h"

#include <stdio.h>
#include <opencv2/opencv.hpp>
#include <fstream>

using namespace std;
using namespace cv;

int HasherObject::load_hashcodes() {
    cout << "Loading hashcodes..." << endl;
    // If we are updating
    itq.release();
    itq.create(data_num, int_num, CV_32SC1);
    char * read_pos = (char*)itq.data;
    for (int i=0; i < update_hash_files.size(); i++)
    {
        read_in.open(update_hash_files[i],ios::in|ios::binary);
        if (!read_in.is_open())
        {
            cout << "Cannot load the itq updates! File "<< update_hash_files[i] << endl;
            return -1;
        }
        read_size = sizeof(int)*data_nums[i]*int_num;
        read_in.read(read_pos, read_size);
        read_in.close();
        read_pos +=read_size;
    }
}


int HasherObject::load_itq_model() {
    // Read itq model
    // read W
    read_in.open(W_name, ios::in|ios::binary);
    if (!read_in.is_open())
    {
        cout << "Cannot load the W model from " << W_name << endl;
        return -1;
    }
    // If we are updating
    W.release();
    W.create(feature_dim, bit_num, CV_64F);
    read_size = sizeof(double)*feature_dim*bit_num;
    read_in.read((char*)W.data, read_size);
    read_in.close();
    // read mvec
    read_in.open(mvec_name, ios::in|ios::binary);
    if (!read_in.is_open())
    {
        cout << "Cannot load the mvec model from " << mvec_name << endl;
        return -1;
    }
    // If we are updating
    mvec.release();
    mvec.create(1, bit_num, CV_64F);
    read_size = sizeof(double)*bit_num;
    read_in.read((char*)mvec.data, read_size);
    read_in.close();
}


Mat HasherObject::read_feats_from_disk(string filename) {
    // Read count
    int feats_num = (int)filesize(filename)/4/this->feature_dim;
    cout << "[read_feats_from_disk] Reading " << feats_num << " features." << endl;
    // Check input file
    ifstream read_in(filename, ios::in|ios::binary);
    if (!read_in.is_open())
    {
        cout << "[read_feats_from_disk] Cannot load the feature file: " << filename << endl;
        return Mat();
    }
    // Allocate memory
    Mat feats_mat(feats_num, this->feature_dim, CV_32F);
    // Read features
    size_t read_size = sizeof(float)*this->feature_dim*feats_num;
    read_in.read((char*)feats_mat.data, read_size);
    // Finalize reading
    read_in.close();
    cout << "[read_feats_from_disk] Read " << read_size <<  " bytes for " << feats_num << " features." << endl;
    return feats_mat;
}

// // When would that be necessary?
// Mat HasherObject::read_hashcodes_from_disk(string filename) {

// }
        
void HasherObject::set_query_feats_from_disk(string filename) {
    query_feats = read_feats_from_disk(filename);
}


// compute hashcodes from feats
unsigned int* HasherObject::compute_hashcodes_from_feats(Mat feats_mat) {
    // hashing init
    if (norm) {
        for  (int k = 0; k < query_num; k++)
            normalize((float*)feats_mat.data + k*feature_dim, feature_dim);
    }
    // Allocate temporary matrices
    Mat feats_mat_double;
    feats_mat.convertTo(feats_mat_double, CV_64F);
    mvec = repeat(mvec, query_num,1);
    // Project features
    Mat realvalued_hash = feats_mat_double*W-mvec;
    // Binarizing features
    unsigned int * hash_mat = new unsigned int[int_num*query_num];
    for  (int k=0; k < query_num; k++)
    {
        for (int i=0; i < int_num; i++)
        {
            hash_mat[k*this->int_num+i] = 0;
            for (int j=0;j<32;j++)
                if (realvalued_hash.at<double>(k,i*32+j)>0)
                    hash_mat[k*this->int_num+i] += 1<<j;
        }
    }
    // Done hashing features
    return hash_mat;
}


// query methods
// use member query_feats and query_codes
void HasherObject::find_knn() {
    unsigned int* query = query_codes;
    float* query_feature = (float*)query_feats.data;
    vector<mypair> top_hamming;
    init_output_files();
    for (int k=0; k < query_num; k++)
    {
        cout <<  "Looking for similar images of query #" << k+1 << endl;
        // Compute hamming distances between query k and all DB hashcodes
        top_hamming = compute_hamming_dist_onehash(query);
        // Rerank based on real valued features
        postrank = rerank_knn_onesample(query_feature, top_hamming);
        // Write out results
        write_to_output_file(postrank, hamming);
        // Move to next query
        query += int_num;
        query_feature += feature_dim;
    }

    // Clean up?
    delete[] query_codes;
    query_feats.release();
    close_output_files();
}

void HasherObject::find_knn_from_feats(Mat _query_feats) {
    query_codes = compute_hashcodes_from_feats(_query_feats);
    query_feats = _query_feats;
    find_knn();
}

// compute rerank top samples using real valued features
vector<mypairf> HasherObject::rerank_knn_onesample(float* query_feature, vector<mypair> top_hamming) {
    //read needed feature for post ranking
    //t[1]=get_wall_time();
    vector<mypairf> postrank(top_hamming.size());
    char* feature_p = (char*)top_feature_mat.data;
    read_size = sizeof(float)*feature_dim;
    int status = 0;
    int i;
    for (i = 0; i < top_hamming.size(); i++)
    {
        // accum, read_in_compfeatures, read_in_compidx should be set previously
        status = get_onefeatcomp(top_hamming[i].second, read_size, accum, read_in_compfeatures, read_in_compidx, feature_p);
        //status = get_onefeat(hamming[i].second,read_size,accum,read_in_features,feature_p);
        if (status == -1) {
            cout << "[rerank_knn_onesample] Could not get feature " << top_hamming[i].second << ". Exiting." << std::endl;
            // should clean here
            return vector<mypairf>(0);
        }
        feature_p += read_size;
    }
    cout << "[rerank_knn_onesample] Biggest hamming distance is: " << top_hamming[i].first << endl;

    // Why not always use squared euclidean distance?
    float* data_feature;
    if (norm)
    {
        for (int i = 0; i < top_hamming.size(); i++)
        {
            // from relationship L2 distance <-> Cosine similarity rerank with:
            // 1 - sum(query_feature[j]*data_feature[j]) 
            postrank[i] = mypairf(1.0f,top_hamming[i].second);
            // if there are too many queries?
            // if (query_num>read_thres)
            //     data_feature = (float*)top_feature_mat.data+feature_dim*postrank[i].second;
            // else
            data_feature = (float*)top_feature_mat.data+feature_dim*i;

            for (int j=0;j<feature_dim;j++)
            {
                postrank[i].first -= query_feature[j]*data_feature[j];
            }
        }
    }
    else
    {
        for (int i = 0; i < top_hamming.size(); i++)
        {
            postrank[i]= mypairf(0.0f,top_hamming[i].second);
            // if there are too many queries?
            // if (query_num>read_thres)
            //     data_feature = (float*)top_feature_mat.data+feature_dim*postrank[i].second;
            // else
            data_feature = (float*)top_feature_mat.data+feature_dim*i;

            for (int j=0;j<feature_dim;j++)
            {
                postrank[i].first += pow(query_feature[j]-data_feature[j],2);
            }
            //postrank[i].first = sqrt(postrank[i].first);
            // divide by 2 so postrank[i].first is always equal between norm and not norm?
            postrank[i].first /= 2;
        }
    }
    std::sort(postrank.begin(), postrank.end(), comparatorf);
    return postrank;
}

vector<mypair> HasherObject::compute_hamming_dist_onehash(unsigned int* query) {
    
    // Initialize data pointer
    unsigned int * hash_data = (unsigned int*)itq.data;
    // Compute distance for each sample of the DB
    for (int i=0; i < data_num; i++)
    {
        // Initialize hamming distance 0 and sample id i
        hamming[i] = mypair(0,i);
        // Compute hamming distance by sets 32 bits
        for (int j=0; j<int_num; j++)
        {
            unsigned int xnor = query[j]^hash_data[j];
            hamming[i].first += NumberOfSetBits(xnor);
        }
        // Move pointer to next DB hashcode
        hash_data += int_num;
    }
    // Sort results
    sort(hamming.begin(), hamming.end(), comparator);
    // Only get the results we need
    vector<mypair> hamming_out(&hamming[0],&hamming[min((unsigned long long)top_feature, (unsigned long long)hamming.size())]);
    return hamming_out;
}


void HasherObject::write_to_output_file(vector<mypairf> postrank, vector<mypair> hamming) {
    // Output to file
    // First, write samples ids
    for (int i=0; i < postrank.size(); i++) {
        this->outputfile << postrank[i].second << ' ';
        // Also output to detailed hamming file (for debugging)
        if (DEMO == 0) {
            this->outputfile_hamming << postrank[i].second << ' ';
        }
    }
    // Then distances
    for (int i=0; i < postrank.size(); i++) {
        this->outputfile << postrank[i].first << ' ';
        // Also output hamming distances (for debugging)
        if (DEMO == 0) {
            this->outputfile_hamming << hamming[i].first << ' ';
        }
    }
    // Write end of lines to both files
    this->outputfile << endl;
    if (DEMO==0) {
        this->outputfile_hamming << endl;
    }        
}

// Mat HasherObject::find_knn_from_hashcodes(Mat hash_mat) {
//     // should init_output_files be called here?
//     unsigned int * query = hash_mat;
//     for (int k=0; k < this->query_num; k++)
//     {
//         cout <<  "Looking for similar images of query #" << k+1 << endl;
//         // Compute hamming distances between query k and all DB hashcodes
//         top_hamming = compute_hamming_dist_onehash(query, top_feature);
//         // Rerank based on real valued features
//         postrank = rerank_knn_onesample(top_hamming);
//         // Write out results
//         write_to_output_file(postrank, hamming);
//         // Move to next query
//         query += int_num;
//         query_feature +=feature_dim;
//     }
// }

void HasherObject::init_output_files() {
    string outname_sim = outname+"-sim.txt";
    cout <<  "[set_output_files] Will write results to " << outname_sim << endl;
    this->outputfile.open(outname_sim, ios::out);
    if (DEMO==0) {
        string outname_hamming = outname+"-hamming.txt";
        cout <<  "Will write detailed hamming results to " << outname_hamming << endl;
        this->outputfile_hamming.open(outname_hamming,ios::out);
    }
}

void HasherObject::close_output_files() {
    this->outputfile.close();
    if (DEMO==0) {
        this->outputfile_hamming.close();
    }
}

void HasherObject::set_paths() {
    // Update strings used to define itq model filenames
    if (norm)
        str_norm = "_norm";
    else
        str_norm = "";
    bit_string = to_string((long long)bit_num);
    itq_name = "itq" + str_norm + "_" + bit_string;
    W_name = m_base_modelpath + "W" + str_norm + "_" + bit_string;
    mvec_name = m_base_modelpath + "mvec" + str_norm + "_" + bit_string;

    // Update string has suffix
    update_feature_suffix = "" + str_norm;
    update_compfeature_suffix = "_comp" + str_norm;
    update_compidx_suffix = "_compidx" + str_norm;
    update_hash_suffix = "_" + itq_name;
    if (norm)
        update_hash_suffix = "_" + itq_name;
    else
        update_hash_suffix = "";

    // Update files prefix
    update_files_list = m_base_updatepath+m_update_files_listname;
    update_hash_prefix = m_base_updatepath+m_update_hash_folder;
    update_feature_prefix = m_base_updatepath+m_update_feature_folder;
    update_compfeature_prefix = m_base_updatepath+m_update_compfeature_folder;
    update_compidx_prefix = m_base_updatepath+m_update_compidx_folder;

}

int HasherObject::read_update_files() {
    // Reinitialize vector files
    update_hash_files.clear();
    update_compfeature_files.clear();
    update_compidx_files.clear();
    read_in_compfeatures.clear();
    read_in_compidx.clear();
    // Read update files list
    ifstream fu(update_files_list.c_str(),ios::in);
    if (!fu.is_open())
    {
        cout << "No update! Was looking for " << update_files_list << endl;
        perror("");
        return -1;
    }
    else
    {
        // Read all update infos
        string line;
        while (getline(fu, line)) {
            update_hash_files.push_back(update_hash_prefix+line+update_hash_suffix);
            update_compfeature_files.push_back(update_compfeature_prefix+line+update_compfeature_suffix);
            update_compidx_files.push_back(update_compidx_prefix+line+update_compidx_suffix);
        }
    }
    // Read comp features 
    int status = 0;
    status = fill_vector_files(read_in_compfeatures, update_compfeature_files);
    if (status==-1) {
        std::cout << "Could not load compressed features properly. Exiting." << std::endl;
        // Should we clean here
        return -1;
    }
    status = fill_vector_files(read_in_compidx, update_compidx_files);
    if (status==-1) {
        std::cout << "Could not load compressed indices properly. Exiting." << std::endl;
        // Should we clean here
        return -1;
    }

    return 0;
}

void HasherObject::fill_data_nums_accum() {
    data_num = fill_data_nums(update_hash_files, data_nums, bit_num);
    delete[] accum;
    accum = new int[data_nums.size()];
    fill_accum(data_nums, accum);
    // this will overwrite top_feature
    set_top_feature();
    hamming.reserve(data_num);
}

void HasherObject::clean_compfeat_files() {
    for (int i = 1; i<data_nums.size();i++)
    {
        if (read_in_compfeatures[i]->is_open())
            read_in_compfeatures[i]->close();
        
        if (read_in_compidx[i]->is_open())
            read_in_compidx[i]->close();
        delete read_in_compfeatures[i];
        delete read_in_compidx[i];
    }
}

// // io from memory - TO BE DONE
// Need to use boost::python converter for cv::Mat?
// maybe later...
// Mat get_feats_from_memory(void* data);

// Mat get_hashcodes_from_memory(void* data);

       