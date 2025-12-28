
import keras.callbacks
from keras import layers, models, activations, losses, metrics, optimizers
import matplotlib.pyplot as plt
from numpy import array, random
import numpy as np
from keras.utils import to_categorical
import keras.backend as K
import tensorflow as tf
from keras import regularizers
import sys
import multiprocessing
import os
import pathlib
from read_data_pack import ReadDataPack

input_data_path = '/home/nader/workspace/robo/Cyrus2DBase/data/'
output_path = './res/'
rdp = ReadDataPack()
rdp.data_label = 'unum'
rdp.use_pass = False
rdp.processes_number = 100
rdp.pack_number = 20
rdp.use_cluster = True
rdp.counts_file = None
rdp.input_data_path = input_data_path
model_name = 'cyrus2d'
pathlib.Path(output_path).mkdir(parents=True, exist_ok=True)
k_best = 1
processes_number = 100
pack_number = 20
epochs = 100
rdp.read_and_separate_data()
train_features = rdp.train_features
train_labels = rdp.train_labels
test_features = rdp.test_features
test_labels = rdp.test_labels

train_labels = to_categorical(train_labels, num_classes=12)
test_labels = to_categorical(test_labels, num_classes=12)
network = models.Sequential()
network.add(layers.Dense(128, activation=activations.relu, input_shape=(train_features.shape[1],)))
network.add(layers.Dense(64, activation=activations.relu))
network.add(layers.Dense(32, activation=activations.relu))
network.add(layers.Dense(train_labels.shape[1], activation=activations.softmax))


def accuracy(y_true, y_pred):
    y_true = K.cast(y_true, y_pred.dtype)
    y_true = K.argmax(y_true)
    # y_pred1 = K.argmax(y_pred)
    res = K.in_top_k(y_pred, y_true, k_best)
    return res


def accuracy2(y_true, y_pred):
    y_true = K.cast(y_true, y_pred.dtype)
    y_true = K.argmax(y_true)
    # y_pred1 = K.argmax(y_pred)
    res = K.in_top_k(y_pred, y_true, 2)
    return res


my_call_back = [
    keras.callbacks.ModelCheckpoint(
        filepath=output_path + '/' + model_name + '-best_model.{epoch:02d}-{val_accuracy:.3f}-{val_accuracy2:.3f}.h5',
        save_best_only=True, monitor='val_accuracy', mode='max'),
    keras.callbacks.TensorBoard(log_dir=output_path)
]
network.compile(optimizer=optimizers.Adam(learning_rate=0.0001), loss=losses.categorical_crossentropy,
                metrics=[accuracy, accuracy2])

history = network.fit(train_features, train_labels, epochs=epochs, batch_size=64, callbacks=my_call_back,
                      validation_data=(test_features, test_labels))
history_dict = history.history
loss_values = history_dict['loss']
val_loss_values = history_dict['val_loss']
acc_values = history_dict['accuracy']
val_acc_values = history_dict['val_accuracy']
epochs = range(len(loss_values))
plt.figure(1)
plt.subplot(211)
plt.plot(epochs, loss_values, 'r--', label='Training loss')
plt.plot(epochs, val_loss_values, 'b--', label='Validation loss')
plt.title("train/test loss")
plt.xlabel('Epochs')
plt.ylabel('Loss')
plt.legend()
plt.subplot(212)
plt.plot(epochs, acc_values, 'r--', label='Training mean_squared_error')
plt.plot(epochs, val_acc_values, '--', label='Validation mean_squared_error')
plt.title("train/test acc")
plt.xlabel('Epochs')
plt.ylabel('Acc')
plt.legend()
plt.savefig(os.path.join(output_path, 'fig_' + model_name + '.png'))
network.save(os.path.join(output_path, 'last_network_' + model_name + '.h5'))
file = open(os.path.join(output_path, 'history_' + model_name), 'w')
file.write(str(history_dict))
file.close()
file = open(os.path.join(output_path, 'best_val_acc_' + model_name), 'w')
file.write(str(max(val_acc_values)))
file.close()
