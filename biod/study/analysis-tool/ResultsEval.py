# To add a new cell, type '# %%'
# To add a new markdown cell, type '# %% [markdown]'
# %% # Imports
import sys
import time
import sys  # sys.getsizeof()

from IPython.display import display, HTML, Markdown
import matplotlib.pyplot as plt
from matplotlib import pyplot as plt
import matplotlib
from math import radians, sqrt

import pandas as pd
import numpy as np
import scipy.stats as st

from experiment import Experiment
from fpc_bet_results import FPCBETResults

# https://ipython.readthedocs.io/en/stable/interactive/magics.html#magic-matplotlib
# To open matplotlib in interactive mode
# %matplotlib qt5
# %matplotlib notebook
# For VSCode Interactive
# get_ipython().run_line_magic('matplotlib', "widget")
#! %matplotlib widget
# %matplotlib widget
# %matplotlib gui
# For VSCode No Interactive
# %matplotlib inline
# %matplotlib --list


# %% # Classes

class DataFrameSetAccess:
    '''Provides a quick method of checking if a given row exists in the table.

    This look method takes hundreds of nanoseconds vs other methods that take
    hudreds of micro seconds. Given the amount of times we must query certain
    tables, this order of magnitude difference is unacceptable.
    '''

    def __init__(self, table: pd.DataFrame, cols: list = None):

        if not cols:
            cols = table.columns

        # This is an expensive operation.
        self.set = {tuple(row) for row in np.array(table[cols])}
        # print(f'Cached set takes {sys.getsizeof(self.set)/1024.0}KB of memory.')

    def isin(self, values: tuple) -> bool:
        return values in self.set




def confidence(ups, downs):
    '''This is some algo from a blog that was used for reddit'''

    n = ups + downs

    if n == 0:
        return 0

    z = 1.0  # 1.44 = 85%, 1.96 = 95%
    phat = float(ups) / n
    return ((phat + z*z/(2*n) - z * sqrt((phat*(1-phat)+z*z/(4*n))/n))/(1+z*z/n))


def print_far_value(val: float, comparisons: list = [20, 50, 100, 500]) -> str:
    str = f'{val:.4e}'
    for c in comparisons:
        str += f' = {val * 100 * c*1000:.3f}% of 1/{c}k'
    return str


def discrete_hist(data, discrete_bins=None):
    if discrete_bins is None:
        unique_elements = np.unique(data)
        discrete_bins = np.arange(
            np.min(unique_elements), np.max(unique_elements)+1)
    bins = np.append(discrete_bins, np.max(discrete_bins) + 1)
    hist, bin_edges = np.histogram(data, bins=bins)
    return hist, bin_edges[:-1]


def DoReport(tbl: pd.DataFrame, level: FPCBETResults.SecLevel):
    ####################################################################
    display(Markdown('## Independent Finger'))
    display(Markdown('### General'))
    display(tbl.describe())
    display(tbl.sum())
    data = tbl[level.column_false()]

    # Attempt to plot better
    plt.figure()
    unique_elements = np.unique(data)
    hist, bins = discrete_hist(data, unique_elements)
    plt.bar(bins, hist)
    plt.xticks(bins)
    plt.xlabel('Num False Matches per Finger')
    plt.ylabel('Frequency')
    plt.show()

    display(Markdown('### Confidence'))
    p = tbl[level.column_false()] / tbl[level.column_total()]
    mean = p.mean()
    std = p.std()
    se = std / np.sqrt(p.count())

    print('Mean: ', print_far_value(mean))
    print('Std: ', std)
    print('Sample Std: ', se)
    print('Std Interval: ', [2*se])
    print('95% Interval: ', [print_far_value(
        mean - (2*se), [20]), print_far_value(mean + (2*se), [20])])

    ####################################################################
    display(Markdown('## Group by User'))
    grp_user = tbl.groupby('User').sum()
    grp_user = grp_user.drop(columns=['Finger'])
    display(grp_user)

    plt.figure()
    plt.bar(grp_user.index.to_list(), grp_user[level.column_false()])
    plt.title('False Counts per User ID Across All Fingers')
    plt.xlabel('Finger ID')
    plt.ylabel('False Counts')
    plt.show()

    display(Markdown('### General'))
    display(grp_user.describe())
    display(grp_user.sum())

    display(Markdown('### Confidence'))
    p = grp_user[level.column_false()] / grp_user[level.column_total()]
    mean = p.mean()
    std = p.std()
    se = std / np.sqrt(p.count())

    print('Mean: ', print_far_value(mean))
    print('Std: ', std)
    print('Sample Std: ', se)
    print('Std Interval: ', [2*se])
    print('95% Interval: ', [print_far_value(
        mean - (2*se), [20]), print_far_value(mean + (2*se), [20])])

    ####################################################################
    display(Markdown('## Group by Finger'))
    grp_finger = tbl.groupby('Finger').sum()
    grp_finger = grp_finger.drop(columns=['User'])
    display(grp_finger)

    plt.figure()
    plt.bar(grp_finger.index.to_list(), grp_finger[level.column_false()])
    plt.title('False Counts per Finger ID Across All Users')
    plt.xlabel('Finger ID')
    plt.ylabel('False Counts')
    plt.show()

    display(Markdown('### General'))
    display(grp_finger.describe())
    display(grp_finger.sum())

    display(Markdown('### Confidence'))
    p = grp_finger[level.column_false()] / grp_finger[level.column_total()]
    mean = p.mean()
    std = p.std()
    se = std / np.sqrt(p.count())

    print('Mean: ', print_far_value(mean))
    print('Std: ', std)
    print('Sample Std: ', se)
    print('Std Interval: ', [2*se])
    print('95% Interval: ', [print_far_value(
        mean - (2*se), [20]), print_far_value(mean + (2*se), [20])])


# %%

REPORT_DIR = 'data' # symbolic link to data

bet = FPCBETResults(REPORT_DIR)

# display(Markdown('# TC-01 FAR'))
# tc0_far = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FAR)
# display(tc0_far)

# display(Markdown('# TC-01 FRR'))
# tc0_frr = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FRR)
# display(tc0_frr)

display(Markdown('# TC-01 FAR @ 1/20k'))
tc0_far_20k: pd.DataFrame = bet.read_far_frr_file(
    FPCBETResults.TestCase.TUDisabled,
    FPCBETResults.TableType.FAR,
    [FPCBETResults.SecLevel.Target_20K])
display(tc0_far_20k)
DoReport(tc0_far_20k, FPCBETResults.SecLevel.Target_20K)
# display(tc0_far_20k.describe())
# display(tc0_far_20k.sum())

display(Markdown('# TC-01 FA List Analysis FAR @ 1/20k'))
tc0_far_20k_fa: pd.DataFrame = bet.read_fa_list_file(
    FPCBETResults.TestCase.TUDisabled)
display(tc0_far_20k_fa)
b = Experiment(num_verification=60,
               num_fingers=6,
               num_users=72,
               fa_list=tc0_far_20k_fa)

# %%
#! %time

NUM_SAMPLES = 1
rng = np.random.default_rng()
fa_set_tuple = [Experiment.FalseTableCol.Verify_User.value,
                Experiment.FalseTableCol.Enroll_User.value,
                Experiment.FalseTableCol.Verify_Finger.value,
                Experiment.FalseTableCol.Verify_Sample.value]
fa_set = DataFrameSetAccess(b.FAList(), fa_set_tuple)

# This nieve approach take about 500ms to run one bootstrap sample, without
# actually querying the FA table (replaced with pass).
for s in range(NUM_SAMPLES):
    sample = []
    # 72 users
    for v in rng.choice(b.num_users, size=b.num_users, replace=True):
        # print(v)
        # 71 other template users
        for t in rng.choice(list(range(v)) + list(range(v+1, b.num_users)), size=b.num_users-1, replace=True):
            # 6 fingers
            # TODO: We need enrollment finger choice too.
            for f in rng.choice(b.num_fingers, size=b.num_fingers, replace=True):
                # 60 verification samples
                for a in rng.choice(b.num_verification, size=b.num_verification, replace=True):
                    # b.FAQuery(verify_user_id=v, enroll_finger_id=t, verify_finger_id=f, verify_sample_index=a)
                    # fa = b.FAQuery(verify_user_id=v, enroll_finger_id=t, verify_finger_id=f, verify_sample_index=a)
                    # sample.append(fa.shape[0] != 0)

                    query = (v, t, f, a)
                    sample.append(fa_set.isin(query))
                    # pass
    # print('')
    # print('samples =', sample)
    # print('sum(samples) =', sum(sample))

# %% Attempt to flatten the loops.
#! %time

NUM_SAMPLES = 30
rng = np.random.default_rng()
fa_set_tuple = [Experiment.FalseTableCol.Verify_User.value,
                Experiment.FalseTableCol.Enroll_User.value,
                Experiment.FalseTableCol.Verify_Finger.value,
                Experiment.FalseTableCol.Verify_Sample.value]
fa_set = DataFrameSetAccess(b.FAList(), fa_set_tuple)

# This nieve approach take about 500ms to run one bootstrap sample, without
# actually querying the FA table (replaced with pass).
for s in range(NUM_SAMPLES):
    sample = []
    # 72 users
    sample_verify_users = rng.choice(b.num_users, size=b.num_users, replace=True)
    sample_verify_users = np.repeat(sample_verify_users, b.num_users-1)
    # against 71 other template users
    sample_enroll_users = rng.choice(b.num_users-1, size=b.num_users*(b.num_users-1), replace=True)
    sample_users = np.stack((sample_verify_users, sample_enroll_users), axis=1)

    # Enforce nested loop invariant:
    # Effectively "omit" the verify user id from the enroll users
    sample_users[sample_users[:,0] <= sample_users[:,1]][:,1] += 1

    for v, t in sample_users:
        pass

    # for v in rng.choice(b.num_users, size=b.num_users, replace=True):
    #     # print(v)
    #     # 71 other template users
    #     for t in rng.choice(list(range(v)) + list(range(v+1, b.num_users)), size=b.num_users-1, replace=True):
    #         # 6 fingers
    #         for f in rng.choice(b.num_fingers, size=b.num_fingers, replace=True):
    #             # 60 verification samples
    #             for a in rng.choice(b.num_verification, size=b.num_verification, replace=True):
    #                 # fa = b.FAQuery(verify_user_id=v, enroll_finger_id=t, verify_finger_id=f, verify_sample_index=a)
    #                 # sample.append(fa.shape[0] != 0)
    #                 query = (v, t, f, a)
    #                 sample.append(fa_set.isin(query))
    #                 # print(query)
    #                 # sample.append(fa_set.isin((10012, 10011, 5, 12)))
    #                 # pass
    print('')
    # print('samples =', sample)
    print('sum(samples) =', sum(sample))
# %%

#! %timeit b.FAList()
#! %timeit b.FAQuery()


# %%
print('Query 1 Timing')
#! %timeit b.FAQuery(verify_user_id=10071)
#! %timeit b.FAQuery(verify_user_id=10071, verify_finger_id=0)
#! %timeit b.FAQuery(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60)
#! %timeit b.FAQuery(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057)
#! %timeit b.FAQuery(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057, enroll_finger_id=4)

# %%
print('Query 2 Timing')
#! %timeit b.FAQuery2(verify_user_id=10071)
# %timeit b.FAQuery2(verify_user_id=10071, verify_finger_id=0)
# %timeit b.FAQuery2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60)
# %timeit b.FAQuery2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057)
#! %timeit b.FAQuery2(verify_user_id=10071, verify_finger_id=0, verify_sample_index=60, enroll_user_id=10057, enroll_finger_id=4)

# %%

print('Query 3 Timing')
s = DataFrameSetAccess(b.FAList())
#! %timeit s.isin((10011, 5, 69, 10012, 0, False))

# %%

# %timeit b.FAQuery(verify_user_id=v, enroll_finger_id=t, verify_finger_id=f, verify_sample_index=a)

# tc0_far_20k: pd.DataFrame = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FAR,
#     [FPCBETResults.SecLevel.Target_20K])

# display(Markdown('# TC-01 FAR @ 1/100k'))
# tc0_far_100k: pd.DataFrame = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FAR,
#     [FPCBETResults.SecLevel.Target_100K])
# display(tc0_far_100k)
# DoReport(tc0_far_100k)


# display(Markdown('# TC-01 FRR @ 1/100k'))
# tc0_frr_100k: pd.DataFrame = bet.read_far_frr_file(
#     FPCBETResults.TestCase.TUDisabled,
#     FPCBETResults.TableType.FRR,
#     [FPCBETResults.SecLevel.Target_100K])
# display(tc0_frr_100k)
# DoReport(tc0_far_100k)
