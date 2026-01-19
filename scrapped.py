import numpy as np 
import pandas as pd 
from statsmodels.regression.mixed_linear_model import MixedLM 
import seaborn as sns 
import matplotlib.pyplot as plt 
from sklearn.preprocessing import LabelEncoder 
# data processing 
data = pd.read_csv('Mixed_effects_data.csv') 
print(data.nunique()) 
sleep_order = {'Low': 0, 'Moderate': 1, 'Good': 2, 'Great': 3} 
data['sleep_quality'] = data['sleep_quality'].map(sleep_order) 
le_sleep = LabelEncoder() 
data['sleep_quality'] = le_sleep.fit_transform(data['sleep_quality']) 
data['sleep_quality'] = data['sleep_quality'].astype('category') 
# numeric + centering 
data['sleep_quality_num'] = data['sleep_quality'].cat.codes.astype(float) 
data['sleep_quality_centered'] = data['sleep_quality_num'] - 
data['sleep_quality_num'].mean() 
# adding terms 
data['sleep_quality_sq'] = data['sleep_quality_centered'] ** 2 
data['sleep_quality_cu'] = data['sleep_quality_centered'] ** 3 
caffeine_map = {'None': 0, 'Caffeinated': 2, 'Very Caffeinated': 1} 

data['caffeine_intake'] = data['caffeine_intake'].map(caffeine_map) 
le_caffeine = LabelEncoder() 
data['caffeine_intake'] = le_caffeine.fit_transform(data['caffeine_intake']) 
data['caffeine_intake'] = data['caffeine_intake'].astype('category') 
data['caffeine_intake_num'] = data['caffeine_intake'].cat.codes.astype(float) 
data['caffeine_intake_centered'] = data['caffeine_intake_num'] - 
data['caffeine_intake_num'].mean() 
# check indiv 
data['caffeine_intake_sq'] = data['caffeine_intake_centered'] ** 2 
print(data[['sleep_quality', 'caffeine_intake']].head()) 
print(data[['sleep_quality', 'caffeine_intake']].isna().sum()) 
print(data.head()) 
print(data.info()) 
print(data[['sleep_quality', 'caffeine_intake']].corr()) 
data['const'] = 1 
data['interaction'] = data['sleep_quality_centered'] * data['caffeine_intake_centered'] 
exog_vars = ['const', 'sleep_quality_centered', 'sleep_quality_sq', 'sleep_quality_cu', 
'caffeine_intake_centered', 
'caffeine_intake_sq', 'interaction'] 

model = MixedLM(endog=data['ln_reaction_time'], 
exog=data[exog_vars], 
groups=data['trial_num']) 
result = model.fit() 
print(result.summary()) 
print(result.random_effects) 
print(result.resid) 
plt.figure(figsize=(12, 6)) 
sns.lineplot(x='sleep_quality_centered', y='ln_reaction_time', hue='caffeine_intake', 
data=data) 
plt.title('Effect of Sleep Quality and Caffeine Intake on Reaction Time') 
plt.xlabel('Centered Sleep Quality') 
plt.ylabel('Visual Reaction Time (ms)') 
plt.show() 
residuals = result.resid 
mean_residual = np.mean(residuals) 
print(f"Mean residual: {mean_residual:.2f}") 
plt.figure(figsize=(10, 6)) 
sns.histplot(residuals, kde=True, color='dimgrey', edgecolor='black') 
plt.axvline(mean_residual, color='red', linestyle='--', label=f'Mean: {mean_residual:.2f}') 
plt.title('Distribution of Residuals (element-of_ij)', fontsize=14) 

plt.xlabel('Residual Value', fontsize=12) 
plt.ylabel('Frequency', fontsize=12) 
plt.legend() 
plt.grid(True, linestyle='--', alpha=0.7) 
plt.show() 
predicted = data['ln_reaction_time'] - residuals 
plt.figure(figsize=(10, 6)) 
sns.scatterplot(x=predicted, y=residuals, alpha=0.6, color='black') 
plt.axhline(0, color='red', linestyle='--') 
plt.title('Residuals vs. Predicted Values', fontsize=14) 
plt.xlabel('Predicted Ln(VRT)', fontsize=12) 
plt.ylabel('Residuals', fontsize=12) 
plt.grid(True, linestyle='--', alpha=0.7) 
plt.show()
