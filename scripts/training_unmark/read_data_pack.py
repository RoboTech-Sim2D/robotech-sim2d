from numpy import array, random
import numpy as np
import multiprocessing
import os


class ReadDataPack:
    def __init__(self):
        self.use_all_data = None
        self.data_label = None
        self.processes_number = None
        self.pack_number = None
        self.counts_file = None
        self.input_data_path = None
        self.train_features = None
        self.train_labels = None
        self.test_features = None
        self.test_labels = None

    def get_col_x(self, header_name_to_num):
        cols = list()
        cols.append(['ball_pos_x', -1])
        cols.append(['ball_pos_y', -1])
        cols.append(['ball_pos_r', -1])
        cols.append(['ball_pos_t', -1])

        for p in range(1, 12):
            cols.append(['p_l_' + str(p) + '_unum', -1])
            cols.append(['p_l_' + str(p) + '_pos_x', -1])
            cols.append(['p_l_' + str(p) + '_pos_y', -1])
            cols.append(['p_l_' + str(p) + '_pos_r', -1])
            cols.append(['p_l_' + str(p) + '_pos_t', -1])
            cols.append(['p_l_' + str(p) + '_kicker_x', -1])
            cols.append(['p_l_' + str(p) + '_kicker_y', -1])
            cols.append(['p_l_' + str(p) + '_kicker_r', -1])
            cols.append(['p_l_' + str(p) + '_kicker_t', -1])
            cols.append(['p_l_' + str(p) + '_in_offside', -1])
            cols.append(['p_l_' + str(p) + '_is_kicker', -1])
            cols.append(['p_l_' + str(p) + '_pass_opp_dist', -1])
            cols.append(['p_l_' + str(p) + '_pass_opp_dist_proj_to_opp', -1])
            cols.append(['p_l_' + str(p) + '_pass_opp_dist_proj_to_kicker', -1])
            cols.append(['p_l_' + str(p) + '_pass_opp_open_angle', -1])
            cols.append(['p_l_' + str(p) + '_opp_dist', -1])
            cols.append(['p_l_' + str(p) + '_opp_angle', -1])

        for p in range(1, 12):
            cols.append(['p_r_' + str(p) + '_unum', -1])
            cols.append(['p_r_' + str(p) + '_pos_x', -1])
            cols.append(['p_r_' + str(p) + '_pos_y', -1])
            cols.append(['p_r_' + str(p) + '_pos_r', -1])
            cols.append(['p_r_' + str(p) + '_pos_t', -1])
            cols.append(['p_r_' + str(p) + '_kicker_x', -1])
            cols.append(['p_r_' + str(p) + '_kicker_y', -1])
            cols.append(['p_r_' + str(p) + '_kicker_r', -1])
            cols.append(['p_r_' + str(p) + '_kicker_t', -1])

        for c in range(len(cols)):
            cols[c][1] = header_name_to_num[cols[c][0]]
            cols[c] = cols[c][1]
        return cols

    def get_col_y(self, header_name_to_num):
        cols = []
        cols.append('out_unum')
        cols_numb = []
        for c in range(len(cols)):
            cols_numb.append(header_name_to_num[cols[c]])
        return cols_numb

    def read_file(self, file_path):
        file = open(file_path[0], 'r')
        lines = file.readlines()[:]
        header = lines[0].split(',')[:-1]
        header_name_to_num = {}
        counter = 0
        for h in header:
            header_name_to_num[h] = counter
            counter += 1
        rows = []
        line_number = 0
        for line in lines[1:]:
            line_number += 1
            row = line.split(',')[:]
            if len(row) != len(header):
                print('error in line', line_number, len(row))
                continue
            f_row = []
            for r in row:
                f_row.append(float(r))
            rows.append(f_row)
        return header_name_to_num, rows

    def read_files_pack(self, files):
        a_pool = multiprocessing.Pool(processes=self.processes_number)
        result = a_pool.map(self.read_file, files)
        header = result[0][0]
        rows = []
        for r in result:
            rows += r[1]

        return header, rows

    def read_files(self, path):
        l = os.listdir(path)
        print(path)
        print('file_numbers', len(l))
        files = []
        f_number = 0
        file_counts = len(l) if not self.counts_file else self.counts_file
        print(file_counts)
        for f in l[:file_counts]:
            if f.endswith('csv'):
                files.append([os.path.join(path, f), f_number])
                f_number += 1
        print(f_number)
        all_data_x = None
        all_data_y = None
        for p in range(self.pack_number):
            header_name_to_num, rows = self.read_files_pack(
                files[int(f_number / self.pack_number * p): int(f_number / self.pack_number * (p + 1))])
            all_data = array(rows)
            del rows
            cols = self.get_col_x(header_name_to_num)
            array_cols = array(cols)
            del cols
            data_x = all_data[:, array_cols[:]]

            cols_numb_y = self.get_col_y(header_name_to_num)
            array_cols_numb_y = array(cols_numb_y)
            del cols_numb_y
            data_y = (all_data[:, array_cols_numb_y[:]])
            del all_data
            if all_data_x is None:
                all_data_x = data_x
            else:
                all_data_x = np.concatenate((all_data_x, data_x), axis=0)
            if all_data_y is None:
                all_data_y = data_y
            else:
                all_data_y = np.concatenate((all_data_y, data_y), axis=0)
            print(f'{(p + 1) / self.pack_number * 100}% processed', all_data_x.shape, all_data_y.shape, data_x.shape,
                  data_y.shape)
        return all_data_x, all_data_y

    def read_and_separate_data(self):
        self.train_features = None
        self.train_labels = None
        self.test_features = None
        self.test_labels = None
        print('#'*100, 'start reading')
        data_x, data_y = self.read_files(self.input_data_path)
        data_size = data_x.shape[0]
        train_size = int(data_size * 0.8)

        randomize = np.arange(data_size)
        np.random.shuffle(randomize)
        X = data_x[randomize]
        del data_x
        Y = data_y[randomize]
        del data_y
        self.train_features = X[:train_size]
        self.train_labels = Y[:train_size]
        self.test_features = X[train_size + 1:]
        self.test_labels = Y[train_size + 1:]
        del X
        del Y
