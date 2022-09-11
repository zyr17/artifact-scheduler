import torch
from torch import nn
from tqdm import tqdm
import random
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
from torch.utils.data import Dataset, DataLoader
import wandb
import dotenv

from model import MLP, MLPSetEmb

dotenv.load_dotenv()


SEED = None
if SEED is None:
    SEED = int(random.random() * 100000000)
random.seed(SEED)
np.random.seed(SEED)
torch.manual_seed(SEED)
if torch.cuda.is_available():
    torch.cuda.manual_seed(SEED)
    torch.backends.cudnn.deterministic = True
    torch.use_deterministic_algorithms = True


data_names = ['hp', 'atk', 'def', 'hpp', 'atkp', 'defp', 'em', 'er', 'cr', 
              'cd', 'bar', 'cost', 'set', 'result']
setname = ['flower', 'plume', 'sands', 'goblet', 'circlet']


def process_data(data_folder = 'data', save_pth = 'data.pth'):
    """
    read data of all txt file from a folder. one line contains one data,
    one line with wrong structure will be ignored. a data should like:

    hp:0 atk:0 def:0.1 hpp:0 atkp:0 defp:0.3 em:0 er:0 cr:0 cd:1  bar:31.73 cost:11355 set:flower result:922989.84

    """
    # if os.path.exists(save_pth):
    #     try:
    #         data = torch.load(save_pth)
    #         return data
    #     except Exception:
    #         pass

    files = [os.path.join(data_folder, x) 
             for x in os.listdir(data_folder) if x[-4:] == '.txt']
    raw = []
    for file in files:
        one_raw = open(file).read().strip().split('\n')
        one_raw = [x for x in one_raw if x[:3] == 'hp:']  # remove not data
        for raw_line in one_raw:
            raw_line = [x.split(':') 
                        for x in raw_line.strip().split(' ') if len(x) > 0]
            line = []
            for aa, [n, d] in zip(data_names, raw_line):
                assert aa == n, f"data name not match, expected {aa} but {n}"
                if d in setname:
                    d = setname.index(d)
                line.append(float(d))
            assert len(line) == 14, "data length should be 14"
            raw.append(line)
    raw = torch.tensor(raw)
    # torch.save(raw, save_pth)
    return raw


def data_clean(data, y_max = 99999000, bar_max = 70, cost_max = 14000, 
               cost_min = 10000, set_filter = [], do_normalize = True):
    """
    filter data, data bigger than max will be removed, if set filter is set,
    only choose set number in set filter. if do normalize, will normalize
    based on their max.
    Note y is logged and will never normalize.
    """
    bar_idx = data_names.index('bar')
    cost_idx = data_names.index('cost')
    set_idx = data_names.index('set')
    y_idx = data_names.index('result')
    weight = []
    bar = None
    cost = None
    set = None
    y = None
    for i in range(data.shape[1]):
        if i == bar_idx:
            bar = data[:, i]
        elif i == cost_idx:
            cost = data[:, i]
        elif i == set_idx:
            set = data[:, i]
        elif i == y_idx:
            y = data[:, i]
        else:
            weight.append(data[:, i])
    weight = torch.stack(weight, dim = 1)
    assert (bar is not None and cost is not None 
            and set is not None and y is not None)
    set = set.long()
    select = cost >= cost_min
    for d, b in [[y, y_max], [bar, bar_max], [cost, cost_max]]:
        select = select & (d <= b)
    for s in set_filter:
        select = select & (set == s)
    print(f'input {len(data)} data, select {select.sum()}')
    weight, bar, cost, set, y = [x[select] 
                                 for x in [weight, bar, cost, set, y]]
    logy = y.log()
    if do_normalize:
        bar /= bar_max
        cost = (cost - cost_min) / (cost_max - cost_min)
        # y /= y_max
    return weight, bar, cost, set, logy


def get_dataloader(data, batch_size = 32, train_split = 0.8):
    length = len(data[0])
    for i in data:
        assert len(i) == length
    data = list(zip(*data))
    random.shuffle(data)
    train_split = int(train_split * length)
    train_data = data[:train_split]
    valid_data = data[train_split:]

    class DS(Dataset):
        def __init__(self, data):
            super().__init__()
            self.data = data

        def __len__(self):
            return len(self.data)

        def __getitem__(self, idx):
            return self.data[idx]

    train_data = DS(train_data)
    valid_data = DS(valid_data)
    train_loader = DataLoader(train_data, batch_size, shuffle = True)
    valid_loader = DataLoader(valid_data, batch_size, shuffle = False)
    return train_loader, valid_loader


def draw_avg_dist_plot(x, y, minx, maxx, split = 100):
    """
    based on x, split y and draw their errorbar
    """
    data = [[] for x in range(split)]
    assert len(x) == len(y)
    for ox, oy in zip(x, y):
        if ox >= minx and ox <= maxx:
            ox = int((ox - minx) / (maxx - minx) * split)
            if ox == split:
                ox = split - 1  # for ox = maxx
            data[ox].append(oy)
    data = [np.array(x) for x in data]
    x = np.array([minx + (maxx - minx) * i / split for i in range(split)])
    print([[minx + (maxx - minx) * num / split, len(i)] 
           for num, i in enumerate(data)])
    y = [i.mean() for i in data]
    s = [i.std() for i in data]
    plt.errorbar(x, y, s, marker = '*')
    plt.show()


def train(model, data, test_data, lr = 1e-5, iteration = 10000000, 
          eval_interval = 1000, wandb_name = ''):
    train_loader, valid_loader = get_dataloader(data)
    test_loader, _ = get_dataloader(test_data, train_split = 1)
    optim = torch.optim.Adam(params = model.parameters(), lr = lr)
    loss_func = nn.MSELoss()
    if wandb_name:
        wandb.init(entity = os.environ['WANDB_ENTITY'], 
                   project = os.environ['WANDB_PROJECT'],
                   name = wandb_name)
        wandb.config.seed = SEED
    counter = 0
    best = 1e100
    loss_prev = []
    for epoch in range(iteration):
        if counter > iteration:
            break
        for num, data in enumerate(train_loader):
            print(f'epoch {epoch}: {num}/{len(train_loader)}     ', end = '\r')
            counter += 1
            optim.zero_grad()
            x, y = data[:-1], data[-1]
            pred = model(*x)
            loss = loss_func(pred, y)
            if wandb_name:
                wandb.log({ 'train_loss': loss.item() }, step = counter)
            loss.backward()
            optim.step()
            loss_prev.append(loss.item())
            if counter % eval_interval == 0:
                print(f'train loss {sum(loss_prev) / len(loss_prev):.6f}')
                loss_prev = []
                print('start evaluate                                  ')
                with torch.no_grad():
                    def make_one_test(loader):
                        total_res = []
                        total_y = []
                        for data in tqdm(loader):
                            x, y = data[:-1], data[-1]
                            pred = model(*x)
                            total_res.append(pred)
                            total_y.append(y)
                        total_res = torch.cat(total_res)
                        total_y = torch.cat(total_y)
                        loss = loss_func(total_res, total_y)
                        return loss
                    valid_loss = make_one_test(valid_loader)
                    test_loss = make_one_test(test_loader)
                    if wandb_name:
                        wandb.log({
                            'valid_loss': valid_loss.item(),
                            'test_loss': test_loss.item() }, 
                            step = counter)
                    print(
                        f'evaluate end, '
                        f'valid loss {valid_loss.item():.6f}, '
                        f'test loss {test_loss.item():.6f}'
                    )
                    if loss.item() < best:
                        best = loss.item()
                        torch.save(
                            model.state_dict(), 
                            f'models/{counter:010d}_{loss.item():.6f}.pth'
                        )
    return model


if __name__ == '__main__':
    data = process_data()
    print(data.shape)
    set_filter = [2]
    data_cleaned = data_clean(
        data, 
        set_filter = set_filter, 
        do_normalize = False
    )
    print(data_cleaned[0].shape)
    test_data = process_data('test_data')
    print(test_data.shape)
    test_data_cleaned = data_clean(
        test_data, 
        set_filter = set_filter, 
        do_normalize = False
    )
    print(test_data_cleaned[0].shape)
    # draw_avg_dist_plot(bar, y, 0, 70)
    # model = MLP(13, hidden = [32, 32, 32])
    model = MLP(13)

    train(model, data_cleaned, test_data_cleaned, 
          iteration = 100000000, 
          wandb_name = '32_32_linear_set2')
