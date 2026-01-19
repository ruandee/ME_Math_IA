import pandas as pd 
from statsmodels.regression.mixed_linear_model import MixedLM 
import seaborn as sns 
import matplotlib.pyplot as plt 
from sklearn.preprocessing import LabelEncoder 

#reading data 
data = pd.read_csv('Mixed_effects_data.csv') 
print(data.nunique()) 
sleep_order = {'Low': 0, 'Moderate': 1, 'Good': 2, 'Great': 3} 
data['sleep_quality'] = data['sleep_quality'].map(sleep_order) 
le_sleep = LabelEncoder() 
data['sleep_quality'] = le_sleep.fit_transform(data['sleep_quality']) 
caffeine_map = {'None': 0, 'Caffeinated': 1, 'Very Caffeinated':2 } 
data['caffeine_intake'] = data['caffeine_intake'].map(caffeine_map) 
le_caffeine = LabelEncoder() 
data['caffeine_intake'] = le_caffeine.fit_transform(data['caffeine_intake']) 

# double check categorical 
data['sleep_quality'] = data['sleep_quality'].astype('category') 
data['caffeine_intake'] = data['caffeine_intake'].astype('category') 
print(data[['sleep_quality', 'caffeine_intake']].head()) 
print(data[['sleep_quality', 'caffeine_intake']].isna().sum()) 
print(data.head()) 
30 
print(data.info()) 
print(data[['sleep_quality', 'caffeine_intake']].corr()) 

# interaction 
data['interaction'] = data['sleep_quality'].cat.codes * data['caffeine_intake'].cat.codes 
model = MixedLM(endog=data['reaction_time'], 
exog=data[['sleep_quality', 'caffeine_intake', 'interaction']], 
groups=data['trial_num']) 
result = model.fit() 
u_i = result.random_effects 
e_ij = result.resid 
print(result.summary()) 
print(result.random_effects) 
print(result.resid) 

#coefficients 
fixed_effects = result.params[['sleep_quality', 'caffeine_intake']] 
p_values = result.pvalues[['sleep_quality', 'caffeine_intake']] 
plt.figure(figsize=(12, 6)) 
sns.lineplot(x='sleep_quality', y='reaction_time', hue='caffeine_intake', data=data) 

#sns.lineplot(x='caffeine_intake', y='reaction_time', hue='sleep_quality', data=data) 
#sns.pointplot(x='caffeine_intake', y='reaction_time', hue='sleep_quality', 

#  data=data, errorbar='ci', dodge=True) 
plt.title('Effect of Sleep Quality and Caffeine Intake on Reaction Time') 
31 
plt.xlabel('Sleep Quality') 
#plt.xlabel('Caffeine Intake (mg)') 
plt.ylabel('Visual Reaction Time (ms)') 
plt.show() 

#residuals 
residuals = result.resid 
mean_residual = np.mean(residuals) 
print(f"Mean residual: {mean_residual:.2f}") 
#plt.figure(figsize=(10, 6)) 
sns.histplot(residuals, kde=True, color=’dimgrey’, edgecolor='black') 
plt.axvline(mean_residual, color='red', linestyle='--', label=f'Mean: {mean_residual:.2f}') 
plt.title('Distribution of Residuals (element-of_ij)', fontsize=14) 
plt.xlabel('Residual Value', fontsize=12) 
plt.ylabel('Frequency', fontsize=12) 
plt.legend() 
plt.grid(True, linestyle='--', alpha=0.7) 
plt.show() 

#checking for heteroscedasticity 
predicted = data['reaction_time'] - residuals  # y_hat = y - e_ij 
plt.figure(figsize=(10, 6)) 
sns.scatterplot(x=predicted, y=residuals, alpha=0.6, color='purple') 
32 
plt.axhline(0, color='red', linestyle='--') 
plt.title('Residuals vs. Predicted Values', fontsize=14) 
plt.xlabel('Predicted Reaction Time', fontsize=12) 
plt.ylabel('Residuals', fontsize=12) 
plt.grid(True, linestyle='--', alpha=0.7) 
plt.show() 
