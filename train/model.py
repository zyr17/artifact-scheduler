import torch
from torch import nn


class MLP(nn.Module):
    def __init__(self, input, hidden = [32, 32]):
        super().__init__()
        mlps = []
        last = input
        for i in hidden:
            mlps.append(nn.Linear(last, i))
            mlps.append(nn.ReLU())
            last = i
        mlps.append(nn.Linear(last, 1))
        self.mlps = nn.Sequential(*mlps)

    def forward(self, weight, bar, cost, set):
        bar = bar.unsqueeze(1)
        cost = cost.unsqueeze(1)
        if len(set.shape) == 1:
            set = set.unsqueeze(1)
        x = torch.cat([weight, bar, cost, set], dim = 1)
        x = self.mlps(x)
        return x.squeeze(1)


class MLPSetEmb(MLP):
    def __init__(self, input, set_emb_dim = 4, set_number = 5, 
                 hidden = [32, 32]):
        real_input = input - 1 + set_emb_dim
        super().__init__(real_input, hidden)
        self.emb = nn.Embedding(set_number, set_emb_dim)

    def forward(self, weight, bar, cost, set):
        set = self.emb(set)
        return super().forward(weight, bar, cost, set)
