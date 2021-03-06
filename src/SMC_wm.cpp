/***********************************************************************************************
 * Copyright (C) 2014 Qinsi Wang.  All rights reserved.
 * Note: part of the implementation of different statistical testing classes are from the statistical model checker developed by Paolo Zuliani.
 * By using this software the USER indicates that he or she has read, understood and will comply
 * with the following:
 *
 * 1. The USER is hereby granted non-exclusive permission to use, copy and/or
 * modify this software for internal, non-commercial, research purposes only. Any
 * distribution, including commercial sale or license, of this software, copies of
 * the software, its associated documentation and/or modifications of either is
 * strictly prohibited without the prior consent of the authors. Title to copyright
 * to this software and its associated documentation shall at all times remain with
 * the authors. Appropriated copyright notice shall be placed on all software
 * copies, and a complete copy of this notice shall be included in all copies of
 * the associated documentation. No right is granted to use in advertising,
 * publicity or otherwise any trademark, service mark, or the name of the authors.
 *
 * 2. This software and any associated documentation is provided "as is".
 *
 * THE AUTHORS MAKE NO REPRESENTATIONS OR WARRANTIES, EXPRESSED OR IMPLIED,
 * INCLUDING THOSE OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, OR THAT
 * USE OF THE SOFTWARE, MODIFICATIONS, OR ASSOCIATED DOCUMENTATION WILL NOT
 * INFRINGE ANY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER INTELLECTUAL PROPERTY
 * RIGHTS OF A THIRD PARTY.
 *
 * The authors shall not be liable under any circumstances for any direct,
 * indirect, special, incidental, or consequential damages with respect to any
 * claim by USER or any third party on account of or arising from the use, or
 * inability to use, this software or its associated documentation, even if the
 * authors have been advised of the possibility of those damages.
 * ***********************************************************************************************/

/**
 ** the command line is as follows:
  ./SMC_wm -testfile <testfile> -modelfile <modelfile> -propfile <propertyfile> -interfile <interventionfile> -initfile <initialfile>

where:
    <testfile> is a text file containing a sequence of test specifications, give the path to it;
    
    <modelfile> is the file name and path of the WM model under analysis;
    
    <propertyfile> is the file name and path of properties to be checked;

    <interventionfile> is the file name and path of Intervention to be implemented. This parameter is optional;

    <initialfile> is the file name and path of the initial value of variables in simulation. This parameter is optional;

**/

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_rng.h>
#include <numeric>
#include <sys/types.h>
#include <sys/wait.h>
#include <boost/lexical_cast.hpp>
#include <ctime>
#include <typeinfo>
#include <omp.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include "Tools.h"
#include "ModelSearch.h"
#include <map>
#include"BooleanNet.hpp"
#include "interface.hpp"
using std::cerr;
using std::cout;
using std::endl;
using std::ifstream;
using std::istringstream;
using std::max;
using std::min;
using std::ostringstream;
using std::string;
using std::vector;
using namespace std;
#define MAXLINE 1024
// base class for every statistical test

double tempResult;
class Test
{
protected:
    string args;
    unsigned int out;                     // current result of the test
    unsigned long int samples, successes; // number of samples, successes

public:
    static const unsigned int NOTDONE = 0;
    static const unsigned int DONE = 1;

    // no default constructor
    Test(string v) : args(v), out(NOTDONE), samples(0), successes(0)
    {
    }

    virtual void init() = 0;

    bool done()
    {
        return (out != NOTDONE);
    }

    virtual double getEstimate() = 0;

    virtual void doTest(unsigned long int n, unsigned long int x) = 0;

    virtual void printResult() = 0;
};

// base class for hypothesis tests
class HTest : public Test
{
protected:
    double theta; // threshold
                  // Null hypothesis is (theta, 1)

public:
    static const unsigned int NULLHYP = 2;
    static const unsigned int ALTHYP = 1;

    HTest(string v) : Test(v), theta(0.0)
    {
    }
    double getEstimate()
    {
        return 0;
    }
    void printResult()
    {

        switch (out)
        {
            // print only when the test is finished
        case NOTDONE:
            cerr << "Test.printResult() : test not completed: " << args << endl;
            exit(EXIT_FAILURE);
        case NULLHYP:
            cout << args << ": "
                 << "Accept Null hypothesis";
            break;
        case ALTHYP:
            cout << args << ": "
                 << "Reject Null hypothesis";
            break;
        }
        cout << ", successes = " << successes << ", samples = " << samples << endl;
    }
};

// base class for statistical estimation
class Estim : public Test
{
protected:
    double delta;    // half-interval width
    double c;        // coverage probability
    double estimate; // the estimate

public:
    Estim(string v) : Test(v), delta(0.0), c(0.0), estimate(0.0)
    {
    }

    // defined later because it uses a method from class CHB
    void printResult();
    double getEstimate();
};
double Estim::getEstimate()
{
    return estimate;
}
// Chernoff-Hoeffding bound
class CHB : public Estim
{
private:
    unsigned long int N; // the bound

public:
    CHB(string v) : Estim(v)
    {
        N = 0;
    }

    unsigned long int get_CH_bound()
    {
        if (N == 0)
        {
            cerr << "N has not been set" << endl;
            exit(EXIT_FAILURE);
        }
        return N;
    }

    void init()
    {
        string testName;

        // convert test arguments from string to float
        istringstream inputString(args);
        inputString >> testName >> delta >> c; // by default >> skips whitespaces

        // sanity checks
        if ((delta >= 0.5) || (delta <= 0.0))
        {
            cerr << args << " : must have 0 < delta < 0.5" << endl;
            exit(EXIT_FAILURE);
        }

        if (c <= 0.0)
        {
            cerr << args << " : must have c > 0" << endl;
            exit(EXIT_FAILURE);
        }

        // compute the Chernoff-Hoeffding bound
        N = int(ceil(1 / (2 * pow(delta, 2)) * log(1 / (1 - c))));

        // writes back the test arguments, with proper formatting
        ostringstream tmp;
        tmp << testName << " " << delta << " " << c;
        args = tmp.str();
    }

    void doTest(unsigned long int n, unsigned long int x)
    {

        // a multi-threaded program will overshoot the bound
        if (n >= N)
        {
            out = DONE;
            samples = n;
            successes = x;
            estimate = double(x) / double(n);
        }
    }
};

// class for naive sampling
class NSAM : public Estim
{
private:
    unsigned long int N; // the given sample num

public:
    NSAM(string v) : Estim(v)
    {
        N = 0;
    }

    unsigned long int get_NSAM_samplenum()
    {
        if (N == 0)
        {
            cerr << "N has not been set" << endl;
            exit(EXIT_FAILURE);
        }
        return N;
    }

    void init()
    {
        string testName;

        // convert test arguments from string to float
        istringstream inputString(args);
        inputString >> testName >> c; // by default >> skips whitespaces

        // read the sample num
        N = int(c);

        // writes back the test arguments, with proper formatting
        ostringstream tmp;
        tmp << testName << " " << c;
        args = tmp.str();
    }

    void doTest(unsigned long int n, unsigned long int x)
    {

        // a multi-threaded program will overshoot the bound
        if (n >= N)
        {
            out = DONE;
            samples = n;
            successes = x;
            estimate = double(x) / double(n);
        }
    }
};

// print the results of an estimation object
void Estim::printResult()
{

    // platform-dependent stuff
    string Iam = typeid(*this).name();

    // print only when the test is finished
    switch (out)
    {
    case NOTDONE:
        cerr << "Estim.printResult() : test not completed: " << args << endl;
        exit(EXIT_FAILURE);
    case DONE:
        cout << args << ": estimate = " << estimate << ", successes = " << successes
             << ", samples = " << samples;

        // if called by a CHB object, print the sample size
        // of the Chernoff-Hoeffding bound, as well
        if (Iam.find("CHB", 0) != string::npos)
        {
            if (CHB *ptr = dynamic_cast<CHB *>(this))
            {
                cout << ", C-H bound = " << ptr->get_CH_bound();
            }
            else
            {
                cerr << "dynamic_cast<CHB*> failed." << endl;
                abort();
            }
        }
        cout << endl;
        break;
    }
};

// Bayesian Interval Estimation with Beta prior
//
// Zuliani, Platzer, Clarke. HSCC 2010.
//
//
class BayesEstim : public Estim
{
private:
    double alpha, beta; // Beta prior parameters

public:
    BayesEstim(string v) : Estim(v), alpha(0.0), beta(0.0)
    {
    }

    void init()
    {
        string testName;

        // convert test arguments from string to float
        istringstream inputString(args);
        inputString >> testName >> delta >> c >> alpha >> beta; // by default >> skips whitespaces

        // sanity checks
        if ((delta > 0.5) || (delta <= 0.0))
        {
            cerr << args << " : must have 0 < delta < 0.5" << endl;
            exit(EXIT_FAILURE);
        }

        if (c <= 0.0)
        {
            cerr << args << " : must have c > 0" << endl;
            exit(EXIT_FAILURE);
        }

        if ((alpha <= 0.0) || (beta <= 0.0))
        {
            cerr << args << " : must have alpha, beta > 0" << endl;
            exit(EXIT_FAILURE);
        }

        // writes back the test arguments, with proper formatting
        ostringstream tmp;
        tmp << testName << " " << delta << " " << c << " " << alpha << " " << beta;
        args = tmp.str();
    }

    void doTest(unsigned long int n, unsigned long int x)
    {

        double t0, t1;   // interval bounds
        double postmean; // posterior mean
        double coverage; // computed coverage
        double a, b;

        // compute posterior mean
        a = double(x) + alpha;
        b = double(n + alpha + beta);
        postmean = a / b;

        // compute the boundaries of the interval
        t0 = postmean - delta;
        t1 = postmean + delta;
        if (t1 > 1)
        {
            t1 = 1;
            t0 = 1 - 2 * delta;
        };
        if (t0 < 0)
        {
            t1 = 2 * delta;
            t0 = 0;
        };

        // compute posterior probability of the interval
        coverage = gsl_cdf_beta_P(t1, a, b - a) - gsl_cdf_beta_P(t0, a, b - a);

        // check if done
        if (coverage >= c)
        {
            out = DONE;
            estimate = postmean;
            samples = n;
            successes = x;
        }
    }
};

//  Lai's test
//
//  Tze Leung Lai
//  "Nearly Optimal Sequential Tests of Composite Hypotheses"
//  The Annals of Statistics
//  1988, 16(2): 856-886
//
//
//  Inputs
//  theta: probability threshold - must satisfy 0 < theta < 1
//  c    : cost per observation
//  n    : number of samples
//  x    : number of successful samples. It must be  x <= n
//
//
//  The Null hypothesis H_0 is the interval [theta, 1]
//
//  Output
//  out : NOTDONE more samples needed
//        ALTHYP is the hypothesis [0, theta]
//        NULLHYP is the hypothesis [theta, 1]
//
//

class Lai : public HTest
{
private:
    double cpo; // cost per observation

    gsl_rng *r; // pseudo-random number generator
    double pi;  // 3.14159

public:
    Lai(string v) : HTest(v), cpo(0.0), r(NULL), pi(0.0)
    {
    }

    void init()
    {
        string testName;

        // convert test arguments from string to float
        istringstream inputString(args);
        inputString >> testName >> theta >> cpo; // by default >> skips whitespaces

        // sanity checks
        if ((theta >= 1.0) || (theta <= 0.0))
        {
            cerr << args << " : must have 0 < theta < 1" << endl;
            exit(EXIT_FAILURE);
        }

        if (cpo <= 0.0)
        {
            cerr << args << " : must have cost > 0" << endl;
            exit(EXIT_FAILURE);
        }

        // initialize pseudo-random number generator
        r = gsl_rng_alloc(gsl_rng_mt19937);
        srand(time(NULL));
        gsl_rng_set(r, rand());

        pi = atan(1) * 4;

        // writes back the test arguments, with proper formatting
        ostringstream tmp;
        tmp << testName << " " << theta << " " << cpo;
        args = tmp.str();
    }

    void doTest(unsigned long int n, unsigned long int x)
    {

        double maxle = double(x) / n; // max likelihood estimate
        double T, t;
        double KL; // Kullback-Leibler information number
        double g, w = 0.0;

        // compute the Kullback-Leibler information number
        if (maxle == 0.0)
            KL = log(1 / (1 - theta));
        else if (maxle == 1.0)
            KL = log(1 / theta);
        else
            KL = maxle * log(maxle / theta) + (1 - maxle) * log((1 - maxle) / (1 - theta));

        // compute function g and the threshold
        t = cpo * n;
        if (t >= 0.8)
        {
            w = 1 / t;
            g = (1 / (16 * pi)) * (pow(w, 2) - (10 / (48 * pi)) * pow(w, 4) + pow(5 / (48 * pi), 2) * pow(w, 6));
        }
        else if ((0.1 <= t) && (t < 0.8))
            g = (exp(-1.38 * t - 2)) / (2 * t);
        else if ((0.01 <= t) && (t < 0.1))
            g = (0.1521 + 0.000225 / t - 0.00585 / sqrt(t)) / (2 * t);
        else
            w = 1 / t;
        g = 0.5 * (2 * log(w) + log(log(w)) - log(4 * pi) - 3 * exp(-0.016 * sqrt(w)));

        T = g / n;

        // check if we are done
        if (KL >= T)
        {
            samples = n;
            successes = x;

            // decide which hypothesis to accept
            if (maxle == theta)
                if (gsl_rng_uniform(r) <= 0.5)
                    out = NULLHYP;
                else
                    out = ALTHYP;
            else if (maxle > theta)
                out = NULLHYP;
            else
                out = ALTHYP;
        }
    }
};

// The Bayes Factor Test with Beta prior
//
//  It computes the Bayes Factor P(data|H_0)/P(data|H_1) and returns
//  whether it is greater/smaller than a specified threshold value or not.
//
//  Inputs
//  theta: probability threshold - must satisfy 0 < theta < 1
//  T    : ratio threshold satisfying T > 1
//  n    : number of samples
//  x    : number of successful samples. It must be  x <= n
//  alpha: Beta prior parameter
//  beta : Beta prior parameter
//  podds: prior odds ratio ( = P(H_1)/P(H_0) )
//
//  The Null hypothesis H_0 is the interval [theta, 1]
//
//  Output
//  out : NOTDONE more samples needed
//        ALTHYP is the hypothesis [0, theta]
//        NULLHYP is the hypothesis [theta, 1]
//

class BFT : public HTest
{
private:
    double T;           // ratio threshold
    double podds;       // prior odds
    double alpha, beta; // Beta prior parameters

public:
    BFT(string v) : HTest(v), T(0.0), podds(0.0), alpha(0.0), beta(0.0)
    {
    }

    void init()
    { // initialize test parameters

        double p0, p1; // prior probabilities
        string testName;

        // convert test arguments from string to double
        istringstream inputString(args);
        inputString >> testName >> theta >> T >> alpha >> beta; // by default >> skips whitespaces

        // sanity checks
        if (T <= 1.0)
        {
            cerr << args << " : must have T > 1" << endl;
            exit(EXIT_FAILURE);
        }

        if ((theta >= 1.0) || (theta <= 0.0))
        {
            cerr << args << " : must have 0 < theta < 1" << endl;
            exit(EXIT_FAILURE);
        }

        if ((alpha <= 0.0) || (beta <= 0.0))
        {
            cerr << args << " : must have alpha, beta > 0" << endl;
            exit(EXIT_FAILURE);
        }

        // compute prior probability of the alternative hypothesis
        p1 = gsl_cdf_beta_P(theta, alpha, beta);

        // sanity check
        if ((p1 >= 1.0) || (p1 <= 0.0))
        {
            cerr << args << " : Prob(H_1) is either 0 or 1" << endl;
            exit(EXIT_FAILURE);
        }
        p0 = 1 - p1;

        // compute prior odds
        podds = p1 / p0;

        // writes back the test arguments, with proper formatting
        ostringstream tmp;
        tmp << testName << " " << theta << " " << T << " " << alpha << " " << beta;
        args = tmp.str();
    }

    void doTest(unsigned long int n, unsigned long int x)
    {

        double B;

        // compute Bayes Factor
        B = podds * (1 / gsl_cdf_beta_P(theta, x + alpha, n - x + beta) - 1);

        // compare and, if done, set
        if (B > T)
        {
            out = NULLHYP;
            samples = n;
            successes = x;
        }
        else if (B < 1 / T)
        {
            out = ALTHYP;
            samples = n;
            successes = x;
        }
    }
};

// The Bayes Factor Test with Beta prior and indifference region
//
// It computes the Bayes Factor P(data|H_0)/P(data|H_1) and returns
// whether it is greater/smaller than a specified threshold value or not.
//
//  Inputs
//  theta1: probability threshold - must satisfy 0 < theta1 < theta2 < 1
//  theta2: probability threshold
//  T    : ratio threshold satisfying T > 1
//  n    : number of samples
//  x    : number of successful samples. It must be  x <= n
//  alpha: Beta prior parameter
//  beta : Beta prior parameter
//  podds: prior odds ratio ( = P(H_1)/P(H_0) )
//
//  The Null hypothesis H_0 is the interval [theta2, 1]
//
//  Output
//  out : NOTDONE more samples needed
//        ALTHYP is the hypothesis [0, theta1]
//        NULLHYP is the hypothesis [theta2, 1]
//
//

class BFTI : public HTest
{
private:
    double T;              // ratio threshold
    double podds;          // prior odds
    double alpha, beta;    // Beta prior parameters
    double delta;          // half indifference region
    double theta1, theta2; // theta1 < theta2 (indifference region)

public:
    BFTI(string v) : HTest(v), T(0.0), podds(0.0), alpha(0.0), beta(0.0), delta(0.0), theta1(0.0), theta2(0.0)
    {
    }

    void init()
    { // initialize test parameters

        double p0, p1; // prior probabilities
        string testName;

        // convert test arguments from string to float
        istringstream inputString(args);
        inputString >> testName >> theta >> T >> alpha >> beta >> delta; // by default >> skips whitespaces

        // sanity checks
        if (T <= 1.0)
        {
            cerr << args << " : must have T > 1" << endl;
            exit(EXIT_FAILURE);
        }

        if ((theta >= 1.0) || (theta <= 0.0))
        {
            cerr << args << " : must have 0 < theta < 1" << endl;
            exit(EXIT_FAILURE);
        }

        if ((alpha <= 0.0) || (beta <= 0.0))
        {
            cerr << args << " : must have alpha, beta > 0" << endl;
            exit(EXIT_FAILURE);
        }

        if ((delta >= 0.5) || (delta <= 0.0))
        {
            cerr << args << " : must have 0 < delta < 0.5" << endl;
            exit(EXIT_FAILURE);
        }

        // prepare parameters
        theta1 = max(0.0, theta - delta);
        theta2 = min(1.0, theta + delta);

        // another sanity check
        if ((theta1 <= 0.0) || (theta2 >= 1.0))
        {
            cerr << args << " : indifference region borders 0 or 1" << endl;
            exit(EXIT_FAILURE);
        }

        // compute prior probability of the alternative hypothesis
        p1 = gsl_cdf_beta_P(theta1, alpha, beta);

        // sanity check
        if ((p1 >= 1.0) || (p1 <= 0.0))
        {
            cerr << args << " : Prob(H_1) is either 0 or 1" << endl;
            exit(EXIT_FAILURE);
        }
        p0 = 1 - p1;

        // compute prior odds
        podds = p1 / p0;

        // writes back the test arguments, with proper formatting
        ostringstream tmp;
        tmp << testName << " " << theta << " " << T << " " << alpha << " " << beta << " " << delta;
        args = tmp.str();
    }

    void doTest(unsigned long int n, unsigned long int x)
    {

        double B;

        // compute Bayes Factor
        B = podds * (1 - gsl_cdf_beta_P(theta2, x + alpha, n - x + beta)) / gsl_cdf_beta_P(theta1, x + alpha, n - x + beta);

        // compare and, if done, set
        if (B > T)
        {
            out = NULLHYP;
            samples = n;
            successes = x;
        }
        else if (B < 1 / T)
        {
            out = ALTHYP;
            samples = n;
            successes = x;
        }
    }
};

// The Sequential Probability Ratio Test
//
//  Inputs
//  theta1, theta2 : probability thresholds satisfying theta1 < theta2
//  T : ratio threshold satisfying T > 1
//  n : number of samples
//  x : number of successful samples. It must be  x <= n
//
//  Output
//  out : NOTDONE more samples needed
//        ALTHYP is the hypothesis [0, theta1]
//        NULLHYP is the hypothesis [theta2, 1]

class SPRT : public HTest
{
private:
    double delta;          // half indifference region
    double theta1, theta2; // theta1 < theta2 (indifference region)
    double T;              // ratio threshold

public:
    SPRT(string v) : HTest(v), delta(0.0), theta1(0.0), theta2(0.0), T(0.0)
    {
    }

    void init()
    { // initialize test parameters

        string testName;

        // convert test arguments from string to float
        istringstream inputString(args);
        inputString >> testName >> theta >> T >> delta; // by default >> skips whitespaces

        // sanity checks
        if (T <= 1.0)
        {
            cerr << args << " : must have T > 1" << endl;
            exit(EXIT_FAILURE);
        }

        if ((theta >= 1.0) || (theta <= 0.0))
        {
            cerr << args << " : must have 0 < theta < 1" << endl;
            exit(EXIT_FAILURE);
        }

        if ((delta >= 0.5) || (delta <= 0.0))
        {
            cerr << args << " : must have 0 < delta < 0.5" << endl;
            exit(EXIT_FAILURE);
        }

        // prepare parameters
        theta1 = max(0.0, theta - delta);
        theta2 = min(1.0, theta + delta);

        // another sanity check
        if ((theta1 <= 0.0) || (theta2 >= 1.0))
        {
            cerr << args << " : indifference region borders 0 or 1" << endl;
            exit(EXIT_FAILURE);
        }

        // writes back the test arguments, with proper formatting
        ostringstream tmp;
        tmp << testName << " " << theta << " " << T << " " << delta;
        args = tmp.str();
    }

    void doTest(unsigned long int n, unsigned long int x)
    {

        double r, t;

        // compute log-ratio and log-threshold
        r = x * log(theta2 / theta1) + (n - x) * log((1 - theta2) / (1 - theta1));
        t = log(T);

        // compare and, if done, set
        if (r > t)
        {
            out = NULLHYP;
            samples = n;
            successes = x;
        }
        else
        {
            if (r < -t)
            {
                out = ALTHYP;
                samples = n;
                successes = x;
            }
        }
    }
};
void getFiles(vector<string> &files, string folder_name, string file_name)
{
    DIR *dp;
    struct dirent *dirp;
    if (!(dp = opendir(folder_name.c_str())))
        printf("can't open");
    string::size_type idx;
    while (((dirp = readdir(dp)) != NULL))
    {
        string a = dirp->d_name;
        idx = a.find(file_name);
        if (idx == string::npos)
        {
        }
        else
            files.push_back(a);
    }
    closedir(dp);
}
string getmodelfilename(string folder_name, string file_name)
{
    vector<string> files;
    getFiles(files, folder_name, file_name);
    int size1 = files.size();
    string newfile = folder_name + "/" + file_name + "_";
    newfile += to_string(size1);
    return newfile;
}
void creatFolder(string pDir)
{
    int i = 0;
    int iRet;
    int iLen;
    char *pszDir;

    pszDir = strdup(pDir.c_str());
    iLen = strlen(pszDir);

    // 创建中间目录
    for (i = 0; i < iLen; i++)
    {
        if (pszDir[i] == '\\' || pszDir[i] == '/')
        {
            pszDir[i] = '\0';

            //如果不存在,创建
            iRet = access(pszDir, 0);
            if (iRet != 0)
            {
                iRet = mkdir(pszDir, 0755);
            }
            //支持linux,将所有\换成/
            pszDir[i] = '/';
        }
    }

    iRet = mkdir(pszDir, 0755);
    free(pszDir);
}
void SMC(map<string, string> mapArgv)
{
    cout << "This is a paralleled version." << endl;
    //cout << mapArgv["-testfile"] << endl;
    //cout << mapArgv["-modelfile"] << endl;
    //cout << mapArgv["-propfile"] << endl;

    bool alldone = false; // all tests done
    bool done;
    unsigned long int satnum = 0; // number of sat samples
    unsigned long int totnum = 0; // number of total samples
    unsigned int numtests = 0;    // number of tests to perform

    vector<string> lines; // variables for string processing
    string line, keyword;

    vector<Test *> myTests; // list of tests to perform
    /*
    if (argc != 7 && argc != 9)
    {
        cout << tools.USAGE << endl;
        cout << "Compiled for OpenMP. Maximum number of threads: " << omp_get_max_threads() << endl
             << endl;
        exit(EXIT_FAILURE);
    }
    */
    /** for first argument - testing file **/
    // read test input file line by line
    ifstream input(mapArgv["-testfile"]);
    if (!input.is_open())
    {
        cerr << "Error: cannot open testfile: " << mapArgv["-testfile"] << endl;
        exit(EXIT_FAILURE);
    }
    while (getline(input, line))
        lines.push_back(line);

    // for each test create object, pass arguments, and initialize
    for (vector<string>::size_type i = 0; i < lines.size(); i++)
    {

        istringstream iline(lines[i]); // each line is a test specification

        // by default, extraction >> skips whitespaces
        keyword = "";
        iline >> keyword;

        // discard comments (lines starting with '#') or empty lines
        if ((keyword.compare(0, 1, "#") != 0) && (keyword.length() > 0))
        {

            transform(keyword.begin(), keyword.end(), keyword.begin(), ::toupper); // convert to uppercase

            // create the corresponding object
            if (keyword == "SPRT")
                myTests.push_back(new SPRT(lines[i]));
            else if (keyword == "BFT")
                myTests.push_back(new BFT(lines[i]));
            else if (keyword == "LAI")
                myTests.push_back(new Lai(lines[i]));
            else if (keyword == "CHB")
                myTests.push_back(new CHB(lines[i]));
            else if (keyword == "BEST")
                myTests.push_back(new BayesEstim(lines[i]));
            else if (keyword == "BFTI")
                myTests.push_back(new BFTI(lines[i]));
            else if (keyword == "NSAM")
                myTests.push_back(new NSAM(lines[i]));
            else
            {
                cerr << "Test unknown: " << lines[i] << endl;
                exit(EXIT_FAILURE);
            }

            myTests[numtests]->init(); // initializes the object
            numtests++;
        }
    }

    if (numtests == 0)
    {
        cout << "No test requested - exiting ..." << endl;
        exit(EXIT_SUCCESS);
    }

    // timing stuff
    time_t start = time(NULL);
    clock_t tic = clock();

    // disable dynamic threads
    omp_set_dynamic(0);

    // get the maximum number of threads
    int maxthreads = omp_get_max_threads();

    // record trace checking result for each thread
    vector<int> result(maxthreads, 0);
    char str_temp[20] = "../trace";
    creatFolder("../trace");
    string folderName = getmodelfilename("../trace", "SMC_wm");
    creatFolder(folderName);
    creatFolder(folderName + "/SAT");
    creatFolder(folderName + "/UNSAT");
    int numTrace = 0;
#pragma omp parallel num_threads(maxthreads) shared(result, alldone, numTrace)
    {
        // @Ziqiang, make sure that TC will return 1 if the trace checkers says unsat, and 0 otherwise
        int ret; // code returned by trace checker

        int tid = omp_get_thread_num();

        // check whether we got all the threads requested
        if (tid == 0)
        {
            if (maxthreads != omp_get_num_threads())
            {
                cerr << "Error: cannot use maximum number of threads" << endl;
                exit(EXIT_FAILURE);
            }
        }

        // @Ziqiang, this is the first place to fill after you modify the ``bound'' input parameter
        // My suggestion is that you may want to add a function in the trace checker that, once it takes
        // in a BLTL, parse the formula and compute the bound in the way that I mention in the wechat.

        // build the command line to call sampler_and_tracechecker (I will call it as TC in the following)
        std::string TCpath = "./Check "; // the path to the exectuable binary of TC, not visible for users
        //std::string callTC = TCpath + string(argv[2]) + " " + string(argv[3]) + " " + folderName; // only model and property are inputs for TC
        std::string callTC = TCpath + mapArgv["-modelfile"] + " " + mapArgv["-propfile"] + " " + folderName;
        if (mapArgv["-interfile"] != "")
            callTC += " " + mapArgv["-interfile"];
        if (mapArgv["-initfile"] != "")
            callTC += " " + mapArgv["-initfile"];
        else
            callTC += " ../testcase/bmi_config.txt";

        ofstream infofile(folderName + "/INFO");
        infofile << "testfile: " << mapArgv["-testfile"] << endl;
        infofile << "modelfile: " << mapArgv["-modelfile"] << endl;
        infofile << "propfile: " << mapArgv["-propfile"] << endl;
        infofile.close();
        //cout<<callTC<<endl;
        while (!alldone)
        {
            string callTC_temp = callTC + " " + to_string(numTrace);
            //cout<<callTC<<endl;

            numTrace += 1;
            // call TC
            /**/
            FILE *fp;
            fp = popen(callTC_temp.c_str(), "r");
            char result_buf[MAXLINE], command[MAXLINE];
            int rc = 0;
            if (NULL == fp)
            {
                cerr << "Error: system() call to the trace checker terminated abnormally: " << callTC_temp << endl;
                exit(EXIT_FAILURE);
            }
            while (fgets(result_buf, sizeof(result_buf), fp) != NULL)
            {
            }
            ret = pclose(fp);
            //cout<<ret<<endl;
            if (ret != 0 && ret != 256)
            {
                cerr << "Error: system() call to the trace checker unsuccessful: " << callTC_temp << endl;
                exit(EXIT_FAILURE);
            }
            ret = WEXITSTATUS(ret);
            //cout<<ret<<"***"<<endl;
            if (ret == 1)
            {
                result[tid] = 1;
            }
            else if (ret == 0)
            {
                result[tid] = 0;
            }
            else
            {
                cerr << "Error: system() call to the trace checker unsuccessful: " << endl;
                exit(EXIT_FAILURE);
            }

#pragma omp barrier
            // only the master thread executes this
            if (tid == 0)
            {
                // update the num of sat samples and total samples
                totnum += maxthreads;
                satnum += accumulate(result.begin(), result.end(), 0);
                result.assign(maxthreads, 0);

                // do all the tests
                alldone = true;
                for (unsigned int j = 0; j < numtests; j++)
                {

                    // do a test, if not done
                    done = myTests[j]->done();
                    if (!done)
                    {
                        myTests[j]->doTest(totnum, satnum);
                        done = myTests[j]->done();
                        if (done)
                            myTests[j]->printResult();
                    }
                    alldone = alldone && done;
                }
            }
#pragma omp barrier
        } //loop

    } // pragma parallel declaration
    if (mapArgv["-getstruct"] == "true")
    {
        vector<string> satmodelfiles;
        string satFolder = folderName + "/SAT";
        getFiles(satmodelfiles, satFolder, "trace");
        ModelSearch MS(folderName);
        MS.getStruct("../tetrad/trace.txt");
        MS.readstruct();
        for (int i = 0; i < satmodelfiles.size(); i++)
        {
            MS.setNewKnowladge();
            satmodelfiles[i] = satFolder + "/" + satmodelfiles[i];
            MS.getStruct(satmodelfiles[i]);
            MS.readstruct();
            cout << satmodelfiles[i] << endl;
        }

        /*
        string callTC1 = "java -jar ../tetrad/tetradcmd-5.0.0-19.jar -data ../tetrad/trace.txt -datatype continuous -algorithm pc -verbose";
        callTC1 += " -graphtxt " + folderName + "/STRUCTINFO/graph.txt";
        callTC1 += " -knowledge " + folderName + "/STRUCTINFO/BN";
        cout << callTC1 << endl;
        FILE *fp1;
        fp1 = popen(callTC1.c_str(), "r");
        */
    }
    cout << "Number of processors: " << omp_get_num_procs() << endl;
    cout << "Number of threads: " << maxthreads << endl;
    cout << "Elapsed cpu time: " << (clock() - tic) / (double)(maxthreads * CLOCKS_PER_SEC) << endl;
    cout << "Elapsed wall time: " << (time(NULL) - start) << endl;
    exit(EXIT_SUCCESS);
}
double getSampleResult(Sampler s, string v, int n, bool r)
{
    double result;
    if (r == 1)
        s.resetBeta();
    if(n>=0)
    {
        for (int i = 0; i < n; i++)
        {
            s.get_one_sample();
        }
    }
    else
    {
        for (int i = 0; i < -n; i++)
        {
            s.getBackwardSample();
        }
    }
    //cout<<s.sample_size<<" "<<s.all_results.size()<<endl;
    //cout << v << " " << n << " " << s.getResult(v, n) << endl;
    return s.getResult(v, n);
}
bool judgeResult(double r, string op, double n)
{
    if (op == ">")
    {
        if (r > n)
            return 1;
        else
            return 0;
    }
    else if (op == "<")
    {
        if (r < n)
            return 1;
        else
            return 0;
    }
    else if (op == "==")
    {
        if (r == n)
            return 1;
        else
            return 0;
    }
    else if (op == ">=")
    {
        if (r >= n)
            return 1;
        else
            return 0;
    }
    else if (op == "<=")
    {
        if (r <= n)
            return 1;
        else
            return 0;
    }
    else
        return 0;
}
double check(Sampler s, string v, int n, vector<string> op, vector<double> x)
{
    //cout << "This is a paralleled version." << endl;

    bool alldone = false; // all tests done
    bool done;
    unsigned long int satnum = 0; // number of sat samples
    unsigned long int totnum = 0; // number of total samples
    unsigned int numtests = 0;    // number of tests to perform
    double checkResult = 0;
    Test *myTests = new BayesEstim("BEST 0.02 0.99 1 1"); //test to perform
    myTests->init();
    omp_set_dynamic(0);

    // get the maximum number of threads
    int maxthreads = omp_get_max_threads();

    // record trace checking result for each thread
    vector<int> result(maxthreads, 0);
#pragma omp parallel num_threads(maxthreads) shared(result, alldone, checkResult)
    {
        int ret; // code returned by trace checker
        int tid = omp_get_thread_num();
        // check whether we got all the threads requested
        if (tid == 0)
        {
            if (maxthreads != omp_get_num_threads())
            {
                cerr << "Error: cannot use maximum number of threads" << endl;
                exit(EXIT_FAILURE);
            }
        }
        while (!alldone)
        {
            int isSat = 1;
            for (int j = 0; j < op.size(); j++)
            {
                bool tempR = judgeResult(getSampleResult(s, v, n, 1), op[j], x[j]);
                if (tempR == 0)
                    isSat = 0;
            }
            result[tid] = isSat;
#pragma omp barrier
            // only the master thread executes this
            if (tid == 0)
            {
                // update the num of sat samples and total samples
                totnum += maxthreads;
                satnum += accumulate(result.begin(), result.end(), 0);
                result.assign(maxthreads, 0);

                // do all the tests
                alldone = true;
                // do a test, if not done
                done = myTests->done();
                if (!done)
                {
                    myTests->doTest(totnum, satnum);
                    done = myTests->done();
                    if (done)
                    {
                        //myTests->printResult();
                        checkResult = myTests->getEstimate();
                    }
                }
                alldone = alldone && done;
            }
#pragma omp barrier
        }
    }
    //cout << "Number of processors: " << omp_get_num_procs() << endl;
    //cout << "Number of threads: " << maxthreads << endl;
    return checkResult;
}
Sampler getSamplerWithoutRandomness(Sampler s)
{
    for (int i = 0; i < s.net_DBN.cpd_list.size(); i++)
    {
        if (s.net_DBN.cpd_list[i].cpd_type == 1)
        {
            s.net_DBN.cpd_list[i].var = 0;
            for (int j = 0; j < s.net_DBN.cpd_list[i].intervention.size(); j++)
            {
                s.net_DBN.cpd_list[i].intervention[j].var = 0;
            }
        }
        else if (s.net_DBN.cpd_list[i].cpd_type == 2)
        {
            s.value[s.NOW][i] = s.net_DBN.cpd_list[i].betaExpexted;
            //cout<<s.net_DBN.cpd_list[i].betaExpexted<<endl;
        }
    }
    return s;
}
double getV1(Sampler s, string v, double e, int n)
{
    int index = s.getVariableX(v);
    if (s.net_DBN.cpd_list[index].cpd_type == 1)
    {
        return s.net_DBN.cpd_list[index].intervention[0].var;
    }
    else if (abs(e) > 3)
        return e / 3;
    else
        return 1;
}
double getV2(Sampler s, string v, double e, int n)
{
    vector<double> r;
    for (int i = 0; i < 100; i++)
    {
        r.push_back(getSampleResult(s, v, n, 1));
    }
    Tools t;
    double var = sqrt(t.getVar(r));
    if (var < 2)
        return 1;
    else
        return var;
}
void setInterval(vector<double> &I, double e, double v, int n)
{
    if (n >= 1)
    {
        v = v * 8 / n;
        for (int i = 0; i < n + 1; i++)
        {
            I.push_back(e - v * n / 2 + i * v);
        }
    }
    else if (n == 0)
    {
        I.push_back(v);
    }
    else
    {
        cout << "Error: Wrong interval number!" << endl;
        exit(EXIT_FAILURE);
    }
}
void setInterval2(vector<double> &I, int n, double l, double r)
{
    if (n < 1)
    {
        cout << "Error: Wrong interval number!" << endl;
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i <= n; i++)
    {
        double temp = 0;
        temp = l + (r - l) / n * i;
        I.push_back(temp);
    }
}
void getPorbDiscute(Sampler S, string v, int n, string output)
{
    if (output == "")
        output = "../Distribution.txt";
    ofstream file_out(output);
    if (!file_out)
    {
        cout << "Error: Wrong path of output file." << endl;
        exit(EXIT_FAILURE);
    }
    vector<double> D;
    double p;
    double sum = 0;
    int index = S.net_BN.cpd_map[v];
    for(int i=0;i<S.net_BN.cpd_list[index].variable_card;i++)
    {
        cout <<"Calculating interal:"<< i << endl;
        vector<string> ops;
        vector<double> conditions;
        ops.push_back("==");
        conditions.push_back(i);
        p = check(S, v, n, ops, conditions);
        D.push_back(p);
        sum+=p;
    }
    file_out << v << "[" << n << "]" << endl;
    file_out << "Interval   Estimated Probability   Normalized Probability" << endl;
    for(int i=0;i<S.net_BN.cpd_list[index].variable_card;i++)
    {
        file_out<<v<<"="<<i<<" "<<D[i]<<D[i]/sum<<endl;
    }
}
void getProb(vector<double> I, Sampler S, string v, int n, string output, int haveRange)
{
    if (output == "")
        output = "../Distribution.txt";
    ofstream file_out(output);
    if (!file_out)
    {
        cout << "Error: Wrong path of output file." << endl;
        exit(EXIT_FAILURE);
    }
    vector<double> D;
    double p;
    if (haveRange == 0)
    {
        for (int i = 0; i < I.size() + 1; i++)
        {
            cout << i << endl;
            vector<string> ops;
            vector<double> conditions;
            if (i == 0)
            {
                ops.push_back("<=");
                conditions.push_back(I[0]);
                p = check(S, v, n, ops, conditions);
                D.push_back(p);
                ops.clear();
                conditions.clear();
                //cout << v << " <= " << I[0] << "   Prob: " << p << endl;
            }
            else if (i < I.size())
            {
                ops.push_back(">");
                conditions.push_back(I[i - 1]);
                ops.push_back("<=");
                conditions.push_back(I[i]);
                p = check(S, v, n, ops, conditions);
                D.push_back(p);
                ops.clear();
                conditions.clear();
                //cout << I[i - 1] << " < " << v << " <= " << I[i] << "   Prob: " << p << endl;
            }
            else if (i == I.size())
            {
                ops.push_back(">");
                conditions.push_back(I[i - 1]);
                p = check(S, v, n, ops, conditions);
                D.push_back(p);
                ops.clear();
                conditions.clear();
                //cout << v << " > " << I[i] << "   Prob: " << (now) << endl;
            }
        }
    }
    else
    {
        for (int i = 0; i < I.size() - 1; i++)
        {
            vector<string> ops;
            vector<double> conditions;
            if (i == 0)
            {
                ops.push_back(">=");
                conditions.push_back(I[i]);
                ops.push_back("<=");
                conditions.push_back(I[i + 1]);
                p = check(S, v, n, ops, conditions);
                D.push_back(p);
                ops.clear();
                conditions.clear();
            }
            else
            {
                ops.push_back(">");
                conditions.push_back(I[i]);
                ops.push_back("<=");
                conditions.push_back(I[i + 1]);
                p = check(S, v, n, ops, conditions);
                D.push_back(p);
                ops.clear();
                conditions.clear();
            }
        }
    }
    double sum = 0;
    for (int i = 0; i < D.size(); i++)
    {
        sum += D[i];
    }
    file_out << v << "[" << n << "]" << endl;
    file_out << "Interval   Estimated Probability   Normalized Probability" << endl;
    for (int i = 0; i < D.size(); i++)
    {
        if (haveRange == 0)
        {
            if (i == 0)
                file_out << "(-∞," << I[0] << "]    " << D[i] << "    " << D[i] / sum << endl;
            else if (i != D.size() - 1)
                file_out << "(" << I[i - 1] << "," << I[i] << "]    " << D[i] << "    " << D[i] / sum << endl;

            else
                file_out << "(" << I[i - 1] << ",+∞)        " << D[i] << "    " << D[i] / sum << endl;
        }
        else
        {
            if (i == 0)
                file_out << "[" << I[i] << "," << I[i + 1] << "]    " << D[i] << "    " << D[i] / sum << endl;
            else
                file_out << "(" << I[i] << "," << I[i + 1] << "]    " << D[i] << "    " << D[i] / sum << endl;
        }
    }
}
void getDistribution(map<string, string> mapArgv)
{
    cout << "This is a paralleled version." << endl;
    time_t start = time(NULL);
    clock_t tic = clock();
    cout << "Loading network." << endl;
    Tools tools;
    string targetVariable = "";
    string tempInt = "";
    int targetTime = -1;
    string tempS = mapArgv["-getDistribution"];
    tools.string_replace(tempS, " ", "");
    int state = 0;
    for (int i = 0; i < tempS.length(); i++)
    {
        if (tempS[i] == '[' || tempS[i] == ']')
        {
            state += 1;
            continue;
        }
        if (state == 0)
        {
            targetVariable += tempS[i];
        }
        else if (state == 1)
        {
            tempInt += tempS[i];
        }
    }
    if (tempInt == "")
    {
        cout << "Error: Please input interval number." << endl;
        cout << tools.USAGE << endl;
        exit(EXIT_FAILURE);
    }
    targetTime = tools.str2int(tempInt);
    cout<<targetTime<<endl;
    if (targetTime == 0)
    {
        cout << "Error: Interval numbers must greater than 1";
        exit(EXIT_FAILURE);
    }
    string modelfile = mapArgv["-modelfile"];
    string interfile = mapArgv["-interfile"];

    string initfile = mapArgv["-initfile"];
    string outputfile = mapArgv["-outputfile"];
    Sampler sample1(modelfile, interfile);
    if (sample1.sampler_type == 1)
    {
        sample1.getInital(initfile);
        cout << "Getting interval information." << endl;
        Sampler sample2 = getSamplerWithoutRandomness(sample1);
        double E = getSampleResult(sample2, targetVariable, targetTime, 0);
        double V = getV2(sample1, targetVariable, E, targetTime);
        vector<double> Interval;
        int intervalNum = tools.str2int(mapArgv["-interval"]);
        int haveRange = 0;
        int vIndex = sample1.getVariableX(targetVariable);
        if (sample1.net_DBN.cpd_list[vIndex].haveRange)
            haveRange = 1;
        else
            haveRange = 0;
        if (haveRange == 0)
        {
            intervalNum -= 2;
            setInterval(Interval, E, V, intervalNum);
        }
        else
            setInterval2(Interval, intervalNum, sample1.net_DBN.cpd_list[vIndex].rangeL, sample1.net_DBN.cpd_list[vIndex].rangeR);
        cout << "Checking..." << endl;
        getProb(Interval, sample1, targetVariable, targetTime, outputfile, haveRange);
    }
    else
    {
        cout << "Checking..." << endl;
        getPorbDiscute(sample1,targetVariable, targetTime, outputfile);
    }
    cout << "Elapsed wall time: " << (time(NULL) - start) << endl;
    exit(EXIT_SUCCESS);
}
void SMC4BL(map<string, string> mapArgv)
{
    Tools tools;
    string file = mapArgv["-tracesfile"];
    BooleanNet b(file);
    //b.showTraces();
    interface I(mapArgv["-propfile"]);
    cout << "This is a paralleled version." << endl;
    bool alldone = false; // all tests done
    bool done;
    unsigned long int satnum = 0; // number of sat samples
    unsigned long int totnum = 0; // number of total samples
    unsigned int numtests = 0;    // number of tests to perform

    vector<string> lines; // variables for string processing
    string line, keyword;

    vector<Test *> myTests; // list of tests to perform
    ifstream input(mapArgv["-testfile"]);
    if (!input.is_open())
    {
        cerr << "Error: cannot open testfile: " << mapArgv["-testfile"] << endl;
        exit(EXIT_FAILURE);
    }
    while (getline(input, line))
        lines.push_back(line);

    // for each test create object, pass arguments, and initialize
    for (vector<string>::size_type i = 0; i < lines.size(); i++)
    {

        istringstream iline(lines[i]); // each line is a test specification

        // by default, extraction >> skips whitespaces
        keyword = "";
        iline >> keyword;

        // discard comments (lines starting with '#') or empty lines
        if ((keyword.compare(0, 1, "#") != 0) && (keyword.length() > 0))
        {

            transform(keyword.begin(), keyword.end(), keyword.begin(), ::toupper); // convert to uppercase

            // create the corresponding object
            if (keyword == "SPRT")
                myTests.push_back(new SPRT(lines[i]));
            else if (keyword == "BFT")
                myTests.push_back(new BFT(lines[i]));
            else if (keyword == "LAI")
                myTests.push_back(new Lai(lines[i]));
            else if (keyword == "CHB")
                myTests.push_back(new CHB(lines[i]));
            else if (keyword == "BEST")
                myTests.push_back(new BayesEstim(lines[i]));
            else if (keyword == "BFTI")
                myTests.push_back(new BFTI(lines[i]));
            else if (keyword == "NSAM")
                myTests.push_back(new NSAM(lines[i]));
            else
            {
                cerr << "Test unknown: " << lines[i] << endl;
                exit(EXIT_FAILURE);
            }

            myTests[numtests]->init(); // initializes the object
            numtests++;
        }
    }

    if (numtests == 0)
    {
        cout << "No test requested - exiting ..." << endl;
        exit(EXIT_SUCCESS);
    }

    // timing stuff
    time_t start = time(NULL);
    clock_t tic = clock();

    // disable dynamic threads
    omp_set_dynamic(0);

    // get the maximum number of threads
    int maxthreads = omp_get_max_threads();

    // record trace checking result for each thread
    vector<int> result(maxthreads, 0);
    int numTrace = 0;
#pragma omp parallel num_threads(maxthreads) shared(result, alldone, numTrace, I)
{
        // @Ziqiang, make sure that TC will return 1 if the trace checkers says unsat, and 0 otherwise
        int ret; // code returned by trace checker

        int tid = omp_get_thread_num();
        // check whether we got all the threads requested
        if (tid == 0)
        {
            if (maxthreads != omp_get_num_threads())
            {
                cerr << "Error: cannot use maximum number of threads" << endl;
                exit(EXIT_FAILURE);
            }
        }
        while (!alldone)
        {
            if(numTrace>=b.sampleNum)
            {
                cerr << "Error: More traces is needed: " << endl;
                for (unsigned int j = 0; j < numtests; j++)
                {
                    if (!done)
                    {
                        myTests[j]->printResult();
                    }
                }
                break;
            }
            interface I1 = I;
            ret = I1.CheckBLTrace(b.varriableList,b.traces[numTrace]);
            numTrace += 1;
            if (ret == 1)
            {
                result[tid] = 1;
            }
            else if (ret == 0)
            {
                result[tid] = 0;
            }
            else
            {
                cerr << "Error: system() call to the trace checker unsuccessful: " << endl;
                exit(EXIT_FAILURE);
            }

#pragma omp barrier
            // only the master thread executes this
            if (tid == 0)
            {
                // update the num of sat samples and total samples
                totnum += maxthreads;
                satnum += accumulate(result.begin(), result.end(), 0);
                result.assign(maxthreads, 0);

                // do all the tests
                alldone = true;
                for (unsigned int j = 0; j < numtests; j++)
                {

                    // do a test, if not done
                    done = myTests[j]->done();
                    if (!done)
                    {
                        myTests[j]->doTest(totnum, satnum);
                        done = myTests[j]->done();
                        if (done)
                            myTests[j]->printResult();
                    }
                    alldone = alldone && done;
                }
            }
#pragma omp barrier
        } //loop
    } // pragma parallel declaration
    cout << "Number of processors: " << omp_get_num_procs() << endl;
    cout << "Number of threads: " << maxthreads << endl;
    cout << "Elapsed cpu time: " << (clock() - tic) / (double)(maxthreads * CLOCKS_PER_SEC) << endl;
    cout << "Elapsed wall time: " << (time(NULL) - start) << endl;
    exit(EXIT_SUCCESS);
}
int main(int argc, char **argv)
{
    Tools tools;
    map<string, string> mapArgv;
    mapArgv = tools.getArgvMap(argc, argv);

    if (mapArgv["-modelfile"] != ""&&mapArgv["-propfile"] != "")
    {
        SMC(mapArgv);
    }
    else if (mapArgv["-modelfile"] != ""&&mapArgv["-getDistribution"] != "")
    {

        getDistribution(mapArgv);
    }
    else if (mapArgv["-tracesfile"] != "")
    {
        SMC4BL(mapArgv);
    }
    else
    {
        cout << "Error:Please input query." << endl;
        exit(EXIT_FAILURE);
    }
}